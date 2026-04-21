#include "cps_generator.h"
#include "code_emitter.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/Support/raw_ostream.h"
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace clang;
using namespace llvm;

namespace cps {

namespace {

// ============================================================
// AST printing helpers
// ============================================================

std::string PrintExpr(const Expr *E, const ASTContext *Ctx) {
  std::string s;
  llvm::raw_string_ostream os(s);
  E->printPretty(os, nullptr, Ctx->getPrintingPolicy());
  os.flush();
  return s;
}

// Print expression, replacing specific sub-expressions with variable names.
// Used to build closure eval bodies where recursive-call holes have been
// substituted by the values received from previous CPS steps.
std::string PrintExprWithReplacements(
    const Expr *E,
    const std::unordered_map<const Expr *, std::string> &Repls,
    const ASTContext *Ctx) {
  if (!E)
    return "";

  auto It = Repls.find(E);
  if (It != Repls.end())
    return It->second;

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    return "(" + PrintExprWithReplacements(BO->getLHS(), Repls, Ctx) + " " +
           BO->getOpcodeStr().str() + " " +
           PrintExprWithReplacements(BO->getRHS(), Repls, Ctx) + ")";
  }

  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    std::string op = UO->getOpcodeStr(UO->getOpcode()).str();
    std::string sub = PrintExprWithReplacements(UO->getSubExpr(), Repls, Ctx);
    if (!UO->isPostfix())
      return op + "(" + sub + ")";
    return "(" + sub + ")" + op;
  }

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    std::string s;
    if (const Expr *Callee = CE->getCallee()) {
      s += PrintExprWithReplacements(Callee, Repls, Ctx);
    }
    s += "(";
    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
      if (i > 0)
        s += ", ";
      s += PrintExprWithReplacements(CE->getArg(i), Repls, Ctx);
    }
    s += ")";
    return s;
  }

  if (const ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
    return "(" + PrintExprWithReplacements(CO->getCond(), Repls, Ctx) + " ? " +
           PrintExprWithReplacements(CO->getTrueExpr(), Repls, Ctx) + " : " +
           PrintExprWithReplacements(CO->getFalseExpr(), Repls, Ctx) + ")";
  }

  if (const ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
    return PrintExprWithReplacements(ASE->getBase(), Repls, Ctx) + "[" +
           PrintExprWithReplacements(ASE->getIdx(), Repls, Ctx) + "]";
  }

  if (const MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
    return PrintExprWithReplacements(ME->getBase(), Repls, Ctx) +
           (ME->isArrow() ? "->" : ".") +
           ME->getMemberNameInfo().getAsString();
  }

  if (const ParenExpr *PE = dyn_cast<ParenExpr>(E)) {
    return "(" + PrintExprWithReplacements(PE->getSubExpr(), Repls, Ctx) + ")";
  }

  if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    return PrintExprWithReplacements(ICE->getSubExpr(), Repls, Ctx);
  }

  if (const CStyleCastExpr *CCE = dyn_cast<CStyleCastExpr>(E)) {
    return "(" + CCE->getTypeAsWritten().getAsString() + ")" +
           "(" + PrintExprWithReplacements(CCE->getSubExpr(), Repls, Ctx) + ")";
  }

  // Fallback: standard pretty-printing.
  return PrintExpr(E, Ctx);
}

// ============================================================
// Hole collection: gather direct recursive calls inside E.
// We do NOT recurse into the arguments of a recursive call itself,
// because those arguments are evaluated before the call.
// ============================================================

void CollectHoles(const Expr *E, const std::string &FuncName,
                  std::vector<CallExpr *> &Holes) {
  if (!E)
    return;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == FuncName) {
        Holes.push_back(const_cast<CallExpr *>(CE));
        return; // do not descend into arguments of the recursive call
      }
    }
  }
  for (const Stmt *Child : E->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)) {
      CollectHoles(ChildExpr, FuncName, Holes);
    }
  }
}

// ============================================================
// Check whether an expression references any function parameters.
// ============================================================

bool ExprUsesParams(const Expr *E,
                    const std::unordered_set<std::string> &ParamNames) {
  if (!E)
    return false;
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const ValueDecl *VD = DRE->getDecl()) {
      if (ParamNames.count(VD->getNameAsString()))
        return true;
    }
  }
  for (const Stmt *Child : E->children()) {
    if (ExprUsesParams(dyn_cast_or_null<Expr>(Child), ParamNames))
      return true;
  }
  return false;
}

// Does this expression (with given holes replaced) or any remaining hole
// arguments reference parameters?
bool NeedsSavedArg(
    const Expr *E, const std::vector<CallExpr *> &Holes,
    size_t HoleIdx, // holes [0..HoleIdx] are already replaced
    const std::unordered_set<std::string> &ParamNames) {
  // Check E excluding already-replaced holes.
  // Simplest conservative check: does the whole E use params?
  // (If a hole uses params, those usages are inside the hole's args and
  // will be evaluated in the CPS function context, not in the closure.)
  if (ExprUsesParams(E, ParamNames))
    return true;

  // Check remaining hole arguments.
  for (size_t i = HoleIdx + 1; i < Holes.size(); ++i) {
    for (unsigned a = 0; a < Holes[i]->getNumArgs(); ++a) {
      if (ExprUsesParams(Holes[i]->getArg(a), ParamNames))
        return true;
    }
  }
  return false;
}

// ============================================================
// Code generation state
// ============================================================

struct GenContext {
  std::string FuncName;
  std::string CPSName;
  std::string ArgType;
  std::string UnitType;
  std::string RetType;
  std::vector<std::string> ParamNames;
  std::unordered_set<std::string> ParamNameSet;
  std::vector<std::string> Closures;
  int NextId = 0;
  const ASTContext *ASTCtx;

  std::string NewClosure() { return "CpsClosure_" + std::to_string(NextId++); }
};

// Forward decl
std::string CpsExpr(const Expr *E, const std::string &ContAccess,
                    GenContext &Ctx);

// ============================================================
// Helpers
// ============================================================

// If E is a direct recursive call f(args), return the Unit expression that
// starts the call with the given continuation. Otherwise return empty.
std::string StartRecursiveCall(const Expr *E, const std::string &ContAccess,
                               GenContext &Ctx) {
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == Ctx.FuncName) {
        std::string args;
        for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
          if (i > 0)
            args += ", ";
          args += PrintExpr(CE->getArg(i), Ctx.ASTCtx);
        }
        return Ctx.UnitType + "(" + Ctx.ArgType + "(" + args + ", " +
               ContAccess + "), " + Ctx.CPSName + ", false)";
      }
    }
  }
  return "";
}

// Build parameter unpack statements inside a closure eval.
std::string BuildUnpacks(const GenContext &Ctx) {
  std::string s = "    auto arg = saved_arg;\n";
  for (const auto &p : Ctx.ParamNames) {
    s += "    auto " + p + " = arg." + p + ";\n";
  }
  return s;
}

// ============================================================
// General CPS expression transformation with holes
// ============================================================

// Build a continuation expression "new ClosureName(v0, v1, ..., saved_arg?, cont)"
// given the precomputed saved-arg flags.
// SavedArgName is the local variable that holds the Arg struct at the call site
// (e.g. "arg" in fib_cps, "saved_arg" in closure eval).
std::string BuildContExpr(const std::string &ClosureName, int upToVIdx,
                          bool NeedsSavedArg,
                          const std::string &SavedArgName,
                          const std::string &ContAccess) {
  std::string s = "new " + ClosureName + "(";
  bool first = true;
  for (int j = 0; j <= upToVIdx; ++j) {
    if (!first) s += ", ";
    s += "v" + std::to_string(j);
    first = false;
  }
  if (NeedsSavedArg) {
    if (!first) s += ", ";
    s += SavedArgName;
    first = false;
  }
  if (!first) s += ", ";
  s += ContAccess + ")";
  return s;
}

std::string CpsExprWithHoles(const Expr *E,
                             const std::vector<CallExpr *> &Holes,
                             const std::string &ContAccess,
                             GenContext &Ctx) {
  if (Holes.empty()) {
    return Ctx.UnitType + "(" + Ctx.ArgType + "(" +
           PrintExprWithReplacements(E, {}, Ctx.ASTCtx) + ", " + ContAccess +
           "), advance, false)";
  }

  // Pre-allocate closure names and compute saved_arg needs for each.
  std::vector<std::string> ClosureNames;
  std::vector<bool> NeedsSaved;
  ClosureNames.reserve(Holes.size());
  NeedsSaved.reserve(Holes.size());
  for (size_t i = 0; i < Holes.size(); ++i) {
    ClosureNames.push_back(Ctx.NewClosure());
    NeedsSaved.push_back(NeedsSavedArg(E, Holes, static_cast<int>(i), Ctx.ParamNameSet));
  }

  // Generate closures from last to first.
  for (int i = static_cast<int>(Holes.size()) - 1; i >= 0; --i) {
    const std::string &cls = ClosureNames[i];
    std::string resultVar = "v" + std::to_string(i);
    bool needsSavedArg = NeedsSaved[i];

    // Build replacement map for holes[0..i].
    std::unordered_map<const Expr *, std::string> repls;
    for (int j = 0; j <= i; ++j) {
      repls[Holes[j]] = "v" + std::to_string(j);
    }

    std::string evalBody;
    if (i == static_cast<int>(Holes.size()) - 1) {
      std::string finalExpr = PrintExprWithReplacements(E, repls, Ctx.ASTCtx);
          evalBody = Ctx.UnitType + "(" + Ctx.ArgType + "(" + finalExpr +
                 ", cont), advance, false)";
    } else {
      std::string nextCont = BuildContExpr(ClosureNames[i + 1], i,
                                           NeedsSaved[i + 1], "saved_arg",
                                           "cont");
      evalBody = StartRecursiveCall(Holes[i + 1], nextCont, Ctx);
    }

    // Build constructor parameter list and initializer list cleanly.
    std::vector<std::string> ctorParams;
    std::vector<std::string> initList;
    for (int j = 0; j < i; ++j) {
      ctorParams.push_back(Ctx.RetType + " v" + std::to_string(j));
      initList.push_back("v" + std::to_string(j) + "(v" + std::to_string(j) + ")");
    }
    if (needsSavedArg) {
      ctorParams.push_back(Ctx.ArgType + " saved_arg");
      initList.push_back("saved_arg(saved_arg)");
    }
    ctorParams.push_back("UtilFunc* cont");
    initList.push_back("cont(cont)");

    std::string ctorDecl = cls + "(";
    for (size_t k = 0; k < ctorParams.size(); ++k) {
      if (k > 0) ctorDecl += ", ";
      ctorDecl += ctorParams[k];
    }
    ctorDecl += ") : ";
    for (size_t k = 0; k < initList.size(); ++k) {
      if (k > 0) ctorDecl += ", ";
      ctorDecl += initList[k];
    }
    ctorDecl += " {}";

    CodeEmitter ce;
    ce.block("class " + cls + " : public UtilFunc", [&](CodeEmitter &e) {
      e.line("public:");

      for (int j = 0; j < i; ++j) {
        e.line(Ctx.RetType + " v" + std::to_string(j) + ";");
      }
      if (needsSavedArg) {
        e.line(Ctx.ArgType + " saved_arg;");
      }
      e.line("UtilFunc* cont;");
      e.line(ctorDecl);

      e.block(Ctx.UnitType + " eval(" + Ctx.RetType + " " + resultVar + ")",
              [&](CodeEmitter &inner) {
                if (needsSavedArg) {
                  inner.raw(BuildUnpacks(Ctx));
                }
                inner.line("return " + evalBody + ";");
              });
    }, ";");

    Ctx.Closures.push_back(ce.str());
  }

  // Initial call.
  std::string initCont = BuildContExpr(ClosureNames[0], -1,
                                       NeedsSaved[0], "arg",
                                       ContAccess);
  return StartRecursiveCall(Holes[0], initCont, Ctx);
}

// ============================================================
// Core CPS expression transformation (entry point)
// ============================================================

std::string CpsExpr(const Expr *E, const std::string &ContAccess,
                    GenContext &Ctx) {
  if (!E)
    return "";

  // Collect all direct recursive calls inside E.
  std::vector<CallExpr *> holes;
  CollectHoles(E, Ctx.FuncName, holes);

  // Case 1: expression IS a recursive call.
  std::string start = StartRecursiveCall(E, ContAccess, Ctx);
  if (!start.empty())
    return start;

  // Case 2: expression contains NO recursive call.
  if (holes.empty()) {
    return Ctx.UnitType + "(" + Ctx.ArgType + "(" +
           PrintExpr(E, Ctx.ASTCtx) + ", " + ContAccess + "), advance, false)";
  }

  // Case 3: general expression with recursive-call holes.
  return CpsExprWithHoles(E, holes, ContAccess, Ctx);
}

// ============================================================
// Tail-recursion analysis
// ============================================================

// Returns true if E is in tail position within Stmt S (or the function body).
// Conservative: we only handle a subset of C++ constructs.
bool IsInTailPosition(const Expr *E, const Stmt *S,
                      const std::string &FuncName) {
  if (!E || !S)
    return false;

  // Direct return statement.
  if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S)) {
    return RS->getRetValue() == E;
  }

  // IfStmt: E is in tail position if it's the return value of either branch.
  if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
    return IsInTailPosition(E, IfS->getThen(), FuncName) ||
           IsInTailPosition(E, IfS->getElse(), FuncName);
  }

  // CompoundStmt: E is in tail position if it's the last statement and
  // that last statement is a return containing E.
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    if (CS->body_empty())
      return false;
    const Stmt *Last = nullptr;
    for (const Stmt *Child : CS->body())
      Last = Child;
    return IsInTailPosition(E, Last, FuncName);
  }

  return false;
}

// Check if a function body has recursive calls only in tail positions.
// If so, the whole function can be converted to a while-loop.
bool IsPureTailRecursive(const FunctionDecl *FD) {
  if (!FD->hasBody())
    return false;

  std::string Name = FD->getNameAsString();

  // Walk the body and collect all recursive calls.
  std::vector<const CallExpr *> AllRecCalls;
  std::function<void(const Stmt *)> collect = [&](const Stmt *S) {
    if (!S)
      return;
    if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
      if (const FunctionDecl *Callee = CE->getDirectCallee()) {
        if (Callee->getNameAsString() == Name)
          AllRecCalls.push_back(CE);
      }
    }
    for (const Stmt *Child : S->children())
      collect(Child);
  };
  collect(FD->getBody());

  if (AllRecCalls.empty())
    return false;

  // Every recursive call must be in tail position.
  for (const CallExpr *CE : AllRecCalls) {
    if (!IsInTailPosition(CE, FD->getBody(), Name))
      return false;
  }
  return true;
}

// ============================================================
// Body analysis (MVP: if/return only)
// ============================================================

bool AnalyzeBody(const Stmt *Body, const Expr *&Cond,
                 const Expr *&BaseExpr, const Expr *&RecExpr,
                 bool &HasElseReturn) {
  HasElseReturn = false;
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(Body)) {
    if (CS->size() == 2) {
      const Stmt *S0 = CS->body_begin()[0];
      const Stmt *S1 = CS->body_begin()[1];
      if (const IfStmt *IfS = dyn_cast<IfStmt>(S0)) {
        Cond = IfS->getCond();
        if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(IfS->getThen())) {
          BaseExpr = RS->getRetValue();
        }
        if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S1)) {
          RecExpr = RS->getRetValue();
          HasElseReturn = true;
        }
      }
    }
  }
  return Cond && BaseExpr && HasElseReturn;
}

} // anonymous namespace

// ============================================================
// Public API
// ============================================================

std::string GenerateCPS(const FunctionDecl *FD) {
  if (!FD || !FD->hasBody())
    return "";

  GenContext Ctx;
  Ctx.FuncName = FD->getNameAsString();
  Ctx.CPSName = Ctx.FuncName + "_cps";
  Ctx.ArgType = Ctx.FuncName + "Arg";
  Ctx.UnitType = "Unit<" + Ctx.ArgType + ">";
  Ctx.ASTCtx = &FD->getASTContext();
  Ctx.RetType = FD->getReturnType().getAsString();

  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    std::string pname = FD->getParamDecl(i)->getNameAsString();
    Ctx.ParamNames.push_back(pname);
    Ctx.ParamNameSet.insert(pname);
  }

  // Analyze body.
  const Expr *Cond = nullptr;
  const Expr *BaseExpr = nullptr;
  const Expr *RecExpr = nullptr;
  bool HasElseReturn = false;
  if (!AnalyzeBody(FD->getBody(), Cond, BaseExpr, RecExpr, HasElseReturn)) {
    errs() << "[cps-transpiler] Function body not in supported MVP shape "
              "(expected: if (cond) return base; return recursive;)\n";
    return "";
  }

  // ==========================================================
  // Tail-recursion optimization
  // ==========================================================
  if (IsPureTailRecursive(FD)) {
    CodeEmitter e;
    e.raw("// === Generated tail-recursion optimized code for function: " +
          Ctx.FuncName + " ===\n\n");

    // Function signature.
    std::string sig = Ctx.RetType + " " + Ctx.FuncName + "(";
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (i > 0)
        sig += ", ";
      sig += FD->getParamDecl(i)->getType().getAsString() + " " +
             FD->getParamDecl(i)->getNameAsString();
    }
    sig += ")";

    e.block(sig, [&](CodeEmitter &b) {
      b.block("while (1)", [&](CodeEmitter &w) {
        w.line("if (" + PrintExpr(Cond, Ctx.ASTCtx) + ") return " +
               PrintExpr(BaseExpr, Ctx.ASTCtx) + ";");
        // Find the recursive expression (the one in the tail return).
        // For pure tail recursion, RecExpr is a direct recursive call.
        if (const CallExpr *RecCall = dyn_cast<CallExpr>(RecExpr)) {
          for (unsigned i = 0;
               i < FD->getNumParams() && i < RecCall->getNumArgs(); ++i) {
            std::string pName = FD->getParamDecl(i)->getNameAsString();
            w.line("auto new_" + pName + " = " +
                   PrintExpr(RecCall->getArg(i), Ctx.ASTCtx) + ";");
          }
          for (unsigned i = 0;
               i < FD->getNumParams() && i < RecCall->getNumArgs(); ++i) {
            std::string pName = FD->getParamDecl(i)->getNameAsString();
            w.line(pName + " = new_" + pName + ";");
          }
        }
      });
    });

    return e.str();
  }

  // ==========================================================
  // Emit generated CPS code
  // ==========================================================
  CodeEmitter e;

  e.raw("// === Generated CPS code for function: " + Ctx.FuncName +
        " ===\n\n");
  e.line("class UtilFunc;");
  e.nl();

  // Arg struct.
  {
    std::string header = "struct " + Ctx.ArgType;
    e.block(header, [&](CodeEmitter &b) {
      for (unsigned i = 0; i < FD->getNumParams(); ++i) {
        b.line(FD->getParamDecl(i)->getType().getAsString() + " " +
               FD->getParamDecl(i)->getNameAsString() + ";");
      }
      b.line("UtilFunc* f;");
      // Constructor.
      std::string ctor = Ctx.ArgType + "(";
      for (unsigned i = 0; i < FD->getNumParams(); ++i) {
        if (i > 0)
          ctor += ", ";
        ctor += FD->getParamDecl(i)->getType().getAsString() + " " +
                FD->getParamDecl(i)->getNameAsString();
      }
      ctor += ", UtilFunc* f) : ";
      for (unsigned i = 0; i < FD->getNumParams(); ++i) {
        if (i > 0)
          ctor += ", ";
        std::string pName = FD->getParamDecl(i)->getNameAsString();
        ctor += pName + "(" + pName + ")";
      }
      ctor += ", f(f) {}";
      b.line(ctor);
    }, ";");
  }
  e.nl();

  // Unit template.
  e.raw("template <typename Arg>\n");
  e.block("struct Unit", [&](CodeEmitter &b) {
    b.line("Arg arg;");
    b.line("Unit<Arg> (*nextf)(Arg);");
    b.line("bool finished;");
    b.line("Unit(Arg arg, Unit<Arg> (*nextf)(Arg), bool finished)");
    b.line("    : arg(arg), nextf(nextf), finished(finished) {}");
  }, ";");
  e.nl();

  // Forward declarations.
  e.line(Ctx.UnitType + " advance(" + Ctx.ArgType + ");");
  e.line(Ctx.UnitType + " " + Ctx.CPSName + "(" + Ctx.ArgType + ");");
  e.nl();

  // UtilFunc base.
  e.block("class UtilFunc", [&](CodeEmitter &b) {
    b.line("public:");
    b.block(
        "virtual " + Ctx.UnitType + " eval(" + Ctx.RetType + " x)",
        [&](CodeEmitter &inner) {
          inner.line("return " + Ctx.UnitType + "(" + Ctx.ArgType +
                     "(x, this), advance, true);");
        });
  }, ";");
  e.nl();

  // Generate closures.
  std::string recCpsBody = CpsExpr(RecExpr, "arg.f", Ctx);

  for (const auto &cls : Ctx.Closures) {
    e.raw(cls);
    e.nl();
  }

  // CPS function.
  e.block(Ctx.UnitType + " " + Ctx.CPSName + "(" + Ctx.ArgType + " arg)",
          [&](CodeEmitter &b) {
            for (const auto &p : Ctx.ParamNames) {
              b.line("auto " + p + " = arg." + p + ";");
            }
            b.line("return " + PrintExpr(Cond, Ctx.ASTCtx));
            b.inc();
            b.line("? " + Ctx.UnitType + "(" + Ctx.ArgType + "(" +
                   PrintExpr(BaseExpr, Ctx.ASTCtx) + ", arg.f), advance, false)");
            b.line(": " + recCpsBody + ";");
            b.dec();
          });
  e.nl();

  // advance.
  e.block(Ctx.UnitType + " advance(" + Ctx.ArgType + " arg)",
          [&](CodeEmitter &b) {
            b.line("auto res = arg.f->eval(" +
                   (Ctx.ParamNames.empty() ? std::string("0")
                                            : std::string("arg.") +
                                                  Ctx.ParamNames[0]) +
                   ");");
            b.line("delete arg.f;");
            b.line("return res;");
          });
  e.nl();

  // trampoline.
  e.raw("template <typename Arg>\n");
  e.block("Arg trampoline(Unit<Arg> t)", [&](CodeEmitter &b) {
    b.line("auto pp = t;");
    b.block("while (1)", [&](CodeEmitter &w) {
      w.line("if (pp.finished) return pp.arg;");
      w.line("pp = pp.nextf(pp.arg);");
    });
  });
  e.nl();

  // Wrapper.
  {
    std::string sig = Ctx.RetType + " " + Ctx.FuncName + "(";
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (i > 0)
        sig += ", ";
      sig += FD->getParamDecl(i)->getType().getAsString() + " " +
             FD->getParamDecl(i)->getNameAsString();
    }
    sig += ")";
    e.block(sig, [&](CodeEmitter &b) {
      std::string call = "return trampoline(" + Ctx.UnitType + "(" +
                         Ctx.ArgType + "(";
      for (unsigned i = 0; i < FD->getNumParams(); ++i) {
        if (i > 0)
          call += ", ";
        call += FD->getParamDecl(i)->getNameAsString();
      }
      call += ", new UtilFunc()), " + Ctx.CPSName + ", false)).";
      call += (Ctx.ParamNames.empty() ? "" : Ctx.ParamNames[0]);
      call += ";";
      b.line(call);
    });
  }

  return e.str();
}

} // namespace cps
