#include "cps_generator.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Expr.h"
#include "llvm/Support/raw_ostream.h"
#include <sstream>
#include <vector>

using namespace clang;
using namespace llvm;

namespace cps {

namespace {

// ============================================================
// Helpers
// ============================================================

std::string PrintExpr(const Expr *E, const ASTContext *Ctx) {
  std::string s;
  llvm::raw_string_ostream os(s);
  E->printPretty(os, nullptr, Ctx->getPrintingPolicy());
  os.flush();
  return s;
}

bool ContainsRecursiveCall(const Expr *E, const std::string &FuncName) {
  if (!E) return false;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == FuncName) return true;
    }
  }
  for (const Stmt *Child : E->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)) {
      if (ContainsRecursiveCall(ChildExpr, FuncName)) return true;
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
  std::vector<std::string> Closures;
  int NextId = 0;
  const ASTContext *ASTCtx;

  std::string NewClosure() { return "CpsClosure_" + std::to_string(NextId++); }
};

// Forward decl
std::string CpsExpr(const Expr *E, const std::string &ContAccess,
                    GenContext &Ctx);

// Build parameter unpack statements: "auto p = arg.p;\n"
std::string BuildParamUnpacks(const std::string &ArgVar) {
  // Will be injected where needed; for now placeholder.
  return "";
}

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
          if (i > 0) args += ", ";
          args += PrintExpr(CE->getArg(i), Ctx.ASTCtx);
        }
        return Ctx.UnitType + "(" + Ctx.ArgType + "(" + args + ", " +
               ContAccess + "), " + Ctx.CPSName + ", false)";
      }
    }
  }
  return "";
}

// Sub-helper: build unpack stmts inside a closure eval
std::string BuildUnpacks(const GenContext &Ctx) {
  std::string s = "    auto arg = saved_arg;\n";
  for (const auto &p : Ctx.ParamNames) {
    s += "    auto " + p + " = arg." + p + ";\n";
  }
  return s;
}

// ============================================================
// Core CPS expression transformation
// ============================================================

std::string CpsExpr(const Expr *E, const std::string &ContAccess,
                    GenContext &Ctx) {
  if (!E) return "";

  // --- Case 1: expression IS a recursive call ---
  std::string start = StartRecursiveCall(E, ContAccess, Ctx);
  if (!start.empty()) return start;

  // --- Case 2: expression contains NO recursive call ---
  if (!ContainsRecursiveCall(E, Ctx.FuncName)) {
    return Ctx.UnitType + "(" + Ctx.ArgType + "(" + PrintExpr(E, Ctx.ASTCtx) +
           ", " + ContAccess + "), advance, false)";
  }

  // --- Case 3: Binary operator containing recursive call(s) ---
  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    std::string op = BO->getOpcodeStr().str();
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
    bool lhsRec = ContainsRecursiveCall(LHS, Ctx.FuncName);
    bool rhsRec = ContainsRecursiveCall(RHS, Ctx.FuncName);

    // Both sides recursive (e.g. fib(n-1) + fib(n-2))
    if (lhsRec && rhsRec) {
      // Inner closure: receives RHS result, does final op, passes to cont
      std::string inner = Ctx.NewClosure();
      std::string innerDef =
          "class " + inner + " : public UtilFunc {\n"
          "public:\n"
          "  " + Ctx.RetType + " lval;\n"
          "  UtilFunc* cont;\n"
          "  " + inner + "(" + Ctx.RetType + " lval, UtilFunc* cont) : lval(lval), cont(cont) {}\n"
          "  " + Ctx.UnitType + " eval(" + Ctx.RetType + " rval) {\n"
          "    return " + Ctx.UnitType + "(" + Ctx.ArgType + "(lval " + op +
          " rval, cont), advance, false);\n"
          "  }\n"
          "};\n";
      Ctx.Closures.push_back(innerDef);

      // Compute RHS CPS *before* building outer, because it may add more closures
      std::string rhsCps = CpsExpr(RHS, "new " + inner + "(lval, cont)", Ctx);

      // Outer closure: receives LHS result, starts RHS computation
      std::string outer = Ctx.NewClosure();
      std::string outerDef =
          "class " + outer + " : public UtilFunc {\n"
          "public:\n"
          "  " + Ctx.ArgType + " saved_arg;\n"
          "  UtilFunc* cont;\n"
          "  " + outer + "(" + Ctx.ArgType + " saved_arg, UtilFunc* cont) : "
          "saved_arg(saved_arg), cont(cont) {}\n"
          "  " + Ctx.UnitType + " eval(int lval) {\n" +
          BuildUnpacks(Ctx) +
          "    return " + rhsCps + ";\n"
          "  }\n"
          "};\n";
      Ctx.Closures.push_back(outerDef);

      std::string lhsStart = StartRecursiveCall(LHS, "new " + outer + "(arg, " + ContAccess + ")", Ctx);
      if (!lhsStart.empty()) return lhsStart;
      return CpsExpr(LHS, "new " + outer + "(arg, " + ContAccess + ")", Ctx);
    }

    // Only LHS recursive
    if (lhsRec && !rhsRec) {
      std::string rhsStr = PrintExpr(RHS, Ctx.ASTCtx);
      std::string cls = Ctx.NewClosure();
      std::string clsDef =
          "class " + cls + " : public UtilFunc {\n"
          "public:\n"
          "  " + Ctx.RetType + " saved_rhs;\n"
          "  UtilFunc* cont;\n"
          "  " + cls + "(" + Ctx.RetType + " saved_rhs, UtilFunc* cont) : saved_rhs(saved_rhs), cont(cont) {}\n"
          "  " + Ctx.UnitType + " eval(" + Ctx.RetType + " lval) {\n"
          "    return " + Ctx.UnitType + "(" + Ctx.ArgType + "(lval " + op +
          " saved_rhs, cont), advance, false);\n"
          "  }\n"
          "};\n";
      Ctx.Closures.push_back(clsDef);

      std::string lhsStart = StartRecursiveCall(LHS, "new " + cls + "(" + rhsStr + ", " + ContAccess + ")", Ctx);
      if (!lhsStart.empty()) return lhsStart;
      return CpsExpr(LHS, "new " + cls + "(" + rhsStr + ", " + ContAccess + ")", Ctx);
    }

    // Only RHS recursive
    if (!lhsRec && rhsRec) {
      std::string lhsStr = PrintExpr(LHS, Ctx.ASTCtx);
      std::string cls = Ctx.NewClosure();
      std::string clsDef =
          "class " + cls + " : public UtilFunc {\n"
          "public:\n"
          "  " + Ctx.RetType + " saved_lhs;\n"
          "  UtilFunc* cont;\n"
          "  " + cls + "(" + Ctx.RetType + " saved_lhs, UtilFunc* cont) : saved_lhs(saved_lhs), cont(cont) {}\n"
          "  " + Ctx.UnitType + " eval(" + Ctx.RetType + " rval) {\n"
          "    return " + Ctx.UnitType + "(" + Ctx.ArgType + "(saved_lhs " + op +
          " rval, cont), advance, false);\n"
          "  }\n"
          "};\n";
      Ctx.Closures.push_back(clsDef);

      std::string rhsStart = StartRecursiveCall(RHS, "new " + cls + "(" + lhsStr + ", " + ContAccess + ")", Ctx);
      if (!rhsStart.empty()) return rhsStart;
      return CpsExpr(RHS, "new " + cls + "(" + lhsStr + ", " + ContAccess + ")", Ctx);
    }
  }

  errs() << "[cps-transpiler] Unsupported expression for CPS transformation\n";
  return "";
}

// ============================================================
// Body analysis (MVP: if/return only)
// ============================================================

bool AnalyzeBody(const Stmt *Body, const Expr *&Cond,
                 const Expr *&BaseExpr,     // then-branch return expr
                 const Expr *&RecExpr,      // else-branch return expr
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
  if (!FD || !FD->hasBody()) return "";

  GenContext Ctx;
  Ctx.FuncName = FD->getNameAsString();
  Ctx.CPSName = Ctx.FuncName + "_cps";
  Ctx.ArgType = Ctx.FuncName + "Arg";
  Ctx.UnitType = "Unit<" + Ctx.ArgType + ">";
  Ctx.ASTCtx = &FD->getASTContext();

  // Return type
  Ctx.RetType = FD->getReturnType().getAsString();

  // Parameter names
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    Ctx.ParamNames.push_back(FD->getParamDecl(i)->getNameAsString());
  }

  // Analyze body
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
  // Tail-recursion optimization: if the recursive branch is a
  // direct call func(args) with no post-call computation, emit
  // a simple while-loop instead of full CPS+Trampoline.
  // ==========================================================
  if (const CallExpr *RecCall = dyn_cast<CallExpr>(RecExpr)) {
    if (const FunctionDecl *Callee = RecCall->getDirectCallee()) {
      if (Callee->getNameAsString() == Ctx.FuncName) {
        std::ostringstream os;
        os << "// === Generated tail-recursion optimized code for function: "
           << Ctx.FuncName << " ===\n\n";
        os << Ctx.RetType << " " << Ctx.FuncName << "(";
        for (unsigned i = 0; i < FD->getNumParams(); ++i) {
          if (i > 0) os << ", ";
          std::string pType = FD->getParamDecl(i)->getType().getAsString();
          std::string pName = FD->getParamDecl(i)->getNameAsString();
          os << pType << " " << pName;
        }
        os << ") {\n";
        os << "  while (1) {\n";
        os << "    if (" << PrintExpr(Cond, Ctx.ASTCtx) << ") return "
           << PrintExpr(BaseExpr, Ctx.ASTCtx) << ";\n";
        // Use temporaries to avoid parameter-update ordering issues
        for (unsigned i = 0; i < FD->getNumParams() && i < RecCall->getNumArgs(); ++i) {
          os << "    auto new_" << FD->getParamDecl(i)->getNameAsString()
             << " = " << PrintExpr(RecCall->getArg(i), Ctx.ASTCtx) << ";\n";
        }
        for (unsigned i = 0; i < FD->getNumParams() && i < RecCall->getNumArgs(); ++i) {
          os << "    " << FD->getParamDecl(i)->getNameAsString()
             << " = new_" << FD->getParamDecl(i)->getNameAsString() << ";\n";
        }
        os << "  }\n";
        os << "}\n";
        return os.str();
      }
    }
  }

  // ==========================================================
  // Emit generated CPS code
  // ==========================================================
  std::ostringstream os;

  // --- Forward declarations ---
  os << "// === Generated CPS code for function: " << Ctx.FuncName << " ===\n\n";
  os << "class UtilFunc;\n\n";

  // --- Arg struct ---
  os << "struct " << Ctx.ArgType << " {\n";
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    std::string pName = FD->getParamDecl(i)->getNameAsString();
    std::string pType = FD->getParamDecl(i)->getType().getAsString();
    os << "  " << pType << " " << pName << ";\n";
  }
  os << "  UtilFunc* f;\n";
  os << "  " << Ctx.ArgType << "(";
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    if (i > 0) os << ", ";
    std::string pName = FD->getParamDecl(i)->getNameAsString();
    std::string pType = FD->getParamDecl(i)->getType().getAsString();
    os << pType << " " << pName;
  }
  os << ", UtilFunc* f) : ";
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    if (i > 0) os << ", ";
    std::string pName = FD->getParamDecl(i)->getNameAsString();
    os << pName << "(" << pName << ")";
  }
  os << ", f(f) {}\n";
  os << "};\n\n";

  // --- Unit template (emit only once per translation unit ideally,
  //     but for simplicity emit here) ---
  os << "template <typename Arg>\n";
  os << "struct Unit {\n";
  os << "  Arg arg;\n";
  os << "  Unit<Arg> (*nextf)(Arg);\n";
  os << "  bool finished;\n";
  os << "  Unit(Arg arg, Unit<Arg> (*nextf)(Arg), bool finished)\n";
  os << "      : arg(arg), nextf(nextf), finished(finished) {}\n";
  os << "};\n\n";

  // --- Forward declarations ---
  os << Ctx.UnitType << " advance(" << Ctx.ArgType << ");\n";
  os << Ctx.UnitType << " " << Ctx.CPSName << "(" << Ctx.ArgType << ");\n\n";

  // --- UtilFunc base ---
  os << "class UtilFunc {\n";
  os << "public:\n";
  os << "  virtual " << Ctx.UnitType << " eval(" << Ctx.RetType << " x) {\n";
  os << "    return " << Ctx.UnitType << "(" << Ctx.ArgType << "(x, this), advance, true);\n";
  os << "  }\n";
  os << "};\n\n";

  // --- Generate closures by transforming the recursive expression ---
  // We call CpsExpr on RecExpr with cont = "arg.f" to collect closures.
  std::string recCpsBody = CpsExpr(RecExpr, "arg.f", Ctx);

  // Emit all collected closures
  for (const auto &cls : Ctx.Closures) {
    os << cls << "\n";
  }

  // --- CPS function ---
  os << Ctx.UnitType << " " << Ctx.CPSName << "(" << Ctx.ArgType << " arg) {\n";
  // Unpack parameters for easy expression reuse
  for (const auto &p : Ctx.ParamNames) {
    os << "  auto " << p << " = arg." << p << ";\n";
  }
  os << "  return " << PrintExpr(Cond, Ctx.ASTCtx) << "\n";
  os << "    ? " << Ctx.UnitType << "(" << Ctx.ArgType << "(" <<
        PrintExpr(BaseExpr, Ctx.ASTCtx) << ", arg.f), advance, false)\n";
  os << "    : " << recCpsBody << ";\n";
  os << "}\n\n";

  // --- advance ---
  os << Ctx.UnitType << " advance(" << Ctx.ArgType << " arg) {\n";
  os << "  auto res = arg.f->eval(";
  // Advance passes the value stored in the first param field as the value
  // being returned through the continuation. In our design the Arg carries
  // the current 'result' in its first parameter if we treat it that way,
  // but actually arg.f->eval needs the *value* that the previous step produced.
  // In cps.cc, the value is in arg.x. We use the first param name here.
  if (!Ctx.ParamNames.empty()) {
    os << "arg." << Ctx.ParamNames[0];
  } else {
    os << "0";
  }
  os << ");\n";
  os << "  delete arg.f;\n";
  os << "  return res;\n";
  os << "}\n\n";

  // --- trampoline ---
  os << "template <typename Arg>\n";
  os << "Arg trampoline(Unit<Arg> t) {\n";
  os << "  auto pp = t;\n";
  os << "  while (1) {\n";
  os << "    if (pp.finished) return pp.arg;\n";
  os << "    pp = pp.nextf(pp.arg);\n";
  os << "  }\n";
  os << "}\n\n";

  // --- Wrapper preserving original signature ---
  os << Ctx.RetType << " " << Ctx.FuncName << "(";
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    if (i > 0) os << ", ";
    std::string pType = FD->getParamDecl(i)->getType().getAsString();
    std::string pName = FD->getParamDecl(i)->getNameAsString();
    os << pType << " " << pName;
  }
  os << ") {\n";
  os << "  return trampoline(" << Ctx.UnitType << "(" << Ctx.ArgType << "(";
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    if (i > 0) os << ", ";
    os << FD->getParamDecl(i)->getNameAsString();
  }
  os << ", new UtilFunc()), " << Ctx.CPSName << ", false)).";
  if (!Ctx.ParamNames.empty()) {
    os << Ctx.ParamNames[0];
  }
  os << ";\n";
  os << "}\n";

  return os.str();
}

} // namespace cps
