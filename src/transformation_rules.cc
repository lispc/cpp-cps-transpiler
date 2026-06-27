#include "transformation_rules.h"
#include "code_emitter.h"
#include "transformation_rule.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/OperationKinds.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace clang;

namespace cps {

namespace {

// ============================================================
// Shared rule helpers
// ============================================================

bool IsInTailPosition(const Expr *E, const Stmt *S,
                      const std::string &FuncName) {
  if (!E || !S)
    return false;
  if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S))
    return RS->getRetValue() == E;
  if (const IfStmt *IfS = dyn_cast<IfStmt>(S))
    return IsInTailPosition(E, IfS->getThen(), FuncName) ||
           IsInTailPosition(E, IfS->getElse(), FuncName);
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

bool IsDirectRecursiveCall(const Expr *E, const std::string &FuncName) {
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee())
      return Callee->getNameAsString() == FuncName;
  }
  return false;
}

// ============================================================
// Tail recursion rule
// ============================================================

class TailRecursionRule : public TransformationRule {
public:
  bool applies(const FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override {
    std::vector<const CallExpr *> recCalls;
    std::function<void(const Stmt *)> collect = [&](const Stmt *S) {
      if (!S)
        return;
      if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
        if (const FunctionDecl *Callee = CE->getDirectCallee()) {
          if (Callee->getNameAsString() == Ctx.FuncName)
            recCalls.push_back(CE);
        }
      }
      for (const Stmt *Child : S->children())
        collect(Child);
    };
    collect(FD->getBody());
    if (recCalls.empty())
      return false;
    for (const CallExpr *CE : recCalls) {
      if (!IsInTailPosition(CE, FD->getBody(), Ctx.FuncName))
        return false;
    }
    return true;
  }

  std::string apply(const FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override {
    CodeEmitter e;
    e.raw("// === Generated tail-recursion optimized code for function: " +
          Ctx.FuncName + " ===\n\n");

    std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
    e.block(sig, [&](CodeEmitter &b) {
      b.block("while (1)", [&](CodeEmitter &w) {
        EmitStmts(w, BA.LeadingStmts, Ctx.ASTCtx);
        for (const auto &bc : BA.BaseCases) {
          w.line("if (" + PrintExpr(bc.first, Ctx.ASTCtx) + ") return " +
                 PrintExpr(bc.second, Ctx.ASTCtx) + ";");
        }
        EmitStmts(w, BA.MiddleStmts, Ctx.ASTCtx);
        if (const CallExpr *RecCall = dyn_cast<CallExpr>(BA.RecExpr)) {
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

  const char *name() const override { return "TailRecursionRule"; }
};

// ============================================================
// Accumulator rule
// ============================================================

class AccumulatorRule : public TransformationRule {
public:
  bool applies(const FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override {
    if (BA.BaseCases.size() != 1)
      return false;

    std::vector<CallExpr *> holes;
    CollectHoles(BA.RecExpr, Ctx.FuncName, holes);
    if (holes.size() != 1)
      return false;

    const BinaryOperator *BO = dyn_cast<BinaryOperator>(BA.RecExpr);
    const CallExpr *OuterCE = dyn_cast<CallExpr>(BA.RecExpr);
    if (!BO && !OuterCE)
      return false;

    if (BO) {
      std::string op = BO->getOpcodeStr().str();
      if (op != "+" && op != "*" && op != "|" && op != "^")
        return false;
    } else {
      if (const FunctionDecl *Callee = OuterCE->getDirectCallee()) {
        std::string name = Callee->getNameAsString();
        if (name != "min" && name != "max")
          return false;
        if (OuterCE->getNumArgs() != 2)
          return false;
      } else {
        return false;
      }
    }

    const Expr *LHS = nullptr;
    const Expr *RHS = nullptr;
    if (BO) {
      LHS = BO->getLHS()->IgnoreParenImpCasts();
      RHS = BO->getRHS()->IgnoreParenImpCasts();
    } else {
      LHS = OuterCE->getArg(0)->IgnoreParenImpCasts();
      RHS = OuterCE->getArg(1)->IgnoreParenImpCasts();
    }

    bool lhsRec = (LHS == holes[0]);
    bool rhsRec = (RHS == holes[0]);
    return (lhsRec && !rhsRec) || (!lhsRec && rhsRec);
  }

  std::string apply(const FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override {
    const BinaryOperator *BO = dyn_cast<BinaryOperator>(BA.RecExpr);
    const CallExpr *OuterCE = dyn_cast<CallExpr>(BA.RecExpr);

    std::string op;
    std::string funcName;
    const Expr *RecCall = nullptr;
    const Expr *Step = nullptr;

    std::vector<CallExpr *> holes;
    CollectHoles(BA.RecExpr, Ctx.FuncName, holes);

    if (BO) {
      op = BO->getOpcodeStr().str();
      const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
      const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
      if (LHS == holes[0]) {
        RecCall = LHS;
        Step = BO->getRHS();
      } else {
        RecCall = RHS;
        Step = BO->getLHS();
      }
    } else {
      if (const FunctionDecl *Callee = OuterCE->getDirectCallee())
        funcName = Callee->getNameAsString();
      const Expr *A0 = OuterCE->getArg(0)->IgnoreParenImpCasts();
      const Expr *A1 = OuterCE->getArg(1)->IgnoreParenImpCasts();
      if (A0 == holes[0]) {
        RecCall = A0;
        Step = OuterCE->getArg(1);
      } else {
        RecCall = A1;
        Step = OuterCE->getArg(0);
      }
    }

    CodeEmitter e;
    e.raw("// === Generated accumulator code for function: " + Ctx.FuncName +
          " ===\n\n");

    std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
    e.block(sig, [&](CodeEmitter &b) {
      b.line(Ctx.RetType + " acc = " +
             PrintExpr(BA.BaseCases[0].second, Ctx.ASTCtx) + ";");
      b.block("while (!(" + PrintExpr(BA.BaseCases[0].first, Ctx.ASTCtx) + "))",
              [&](CodeEmitter &w) {
                EmitStmts(w, BA.LeadingStmts, Ctx.ASTCtx);
                EmitStmts(w, BA.MiddleStmts, Ctx.ASTCtx);
                if (!op.empty()) {
                  w.line("acc = acc " + op + " " + PrintExpr(Step, Ctx.ASTCtx) +
                         ";");
                } else {
                  w.line("acc = " + funcName + "(acc, " +
                         PrintExpr(Step, Ctx.ASTCtx) + ");");
                }
                if (const CallExpr *RecCE = dyn_cast<CallExpr>(RecCall)) {
                  for (unsigned i = 0;
                       i < FD->getNumParams() && i < RecCE->getNumArgs(); ++i) {
                    std::string pName = FD->getParamDecl(i)->getNameAsString();
                    w.line("auto new_" + pName + " = " +
                           PrintExpr(RecCE->getArg(i), Ctx.ASTCtx) + ";");
                  }
                  for (unsigned i = 0;
                       i < FD->getNumParams() && i < RecCE->getNumArgs(); ++i) {
                    std::string pName = FD->getParamDecl(i)->getNameAsString();
                    w.line(pName + " = new_" + pName + ";");
                  }
                }
              });
      b.line("return acc;");
    });

    return e.str();
  }

  const char *name() const override { return "AccumulatorRule"; }
};

// ============================================================
// Tupling rule (k-th order linear recurrence)
// ============================================================

namespace {

struct LinearTerm {
  int Order;
  int Sign;
  CallExpr *Hole;
};

bool ParseLinearTerms(const Expr *E, const std::string &FuncName,
                      const std::string &ParamName,
                      std::vector<LinearTerm> &Terms, int &MaxOrder) {
  E = E->IgnoreParenImpCasts();

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == FuncName) {
        if (CE->getNumArgs() != 1)
          return false;
        const Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();
        const BinaryOperator *BO = dyn_cast<BinaryOperator>(Arg);
        if (!BO || BO->getOpcode() != BO_Sub)
          return false;
        const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
        const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
        const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(LHS);
        if (!DRE || DRE->getDecl()->getNameAsString() != ParamName)
          return false;
        const IntegerLiteral *IL = dyn_cast<IntegerLiteral>(RHS);
        if (!IL)
          return false;
        int c = static_cast<int>(IL->getValue().getSExtValue());
        if (c <= 0)
          return false;
        Terms.push_back({c, 1, const_cast<CallExpr *>(CE)});
        MaxOrder = std::max(MaxOrder, c);
        return true;
      }
    }
  }

  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_Minus) {
      std::vector<LinearTerm> SubTerms;
      int SubMax = 0;
      if (!ParseLinearTerms(UO->getSubExpr(), FuncName, ParamName, SubTerms,
                            SubMax))
        return false;
      for (auto &t : SubTerms)
        t.Sign = -t.Sign;
      Terms.insert(Terms.end(), SubTerms.begin(), SubTerms.end());
      MaxOrder = std::max(MaxOrder, SubMax);
      return true;
    }
  }

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() != BO_Add && BO->getOpcode() != BO_Sub)
      return false;
    std::vector<LinearTerm> LeftTerms, RightTerms;
    int LeftMax = 0, RightMax = 0;
    if (!ParseLinearTerms(BO->getLHS(), FuncName, ParamName, LeftTerms, LeftMax))
      return false;
    if (!ParseLinearTerms(BO->getRHS(), FuncName, ParamName, RightTerms,
                          RightMax))
      return false;
    if (BO->getOpcode() == BO_Sub) {
      for (auto &t : RightTerms)
        t.Sign = -t.Sign;
    }
    Terms.insert(Terms.end(), LeftTerms.begin(), LeftTerms.end());
    Terms.insert(Terms.end(), RightTerms.begin(), RightTerms.end());
    MaxOrder = std::max({MaxOrder, LeftMax, RightMax});
    return true;
  }

  return false;
}

enum class EvalResult { True, False, Unknown };

bool ExtractParamOrLiteral(const Expr *E, const std::string &ParamName,
                           int ParamValue, int &Out) {
  E = E->IgnoreParenImpCasts();
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (DRE->getDecl()->getNameAsString() == ParamName) {
      Out = ParamValue;
      return true;
    }
  }
  if (const IntegerLiteral *IL = dyn_cast<IntegerLiteral>(E)) {
    Out = static_cast<int>(IL->getValue().getSExtValue());
    return true;
  }
  return false;
}

EvalResult EvalConditionForParam(const Expr *E, const std::string &ParamName,
                                 int ParamValue) {
  E = E->IgnoreParenImpCasts();

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BO_LAnd) {
      auto L = EvalConditionForParam(BO->getLHS(), ParamName, ParamValue);
      auto R = EvalConditionForParam(BO->getRHS(), ParamName, ParamValue);
      if (L == EvalResult::False || R == EvalResult::False)
        return EvalResult::False;
      if (L == EvalResult::Unknown || R == EvalResult::Unknown)
        return EvalResult::Unknown;
      return EvalResult::True;
    }
    if (BO->getOpcode() == BO_LOr) {
      auto L = EvalConditionForParam(BO->getLHS(), ParamName, ParamValue);
      auto R = EvalConditionForParam(BO->getRHS(), ParamName, ParamValue);
      if (L == EvalResult::True || R == EvalResult::True)
        return EvalResult::True;
      if (L == EvalResult::Unknown || R == EvalResult::Unknown)
        return EvalResult::Unknown;
      return EvalResult::False;
    }

    int lhsVal = 0, rhsVal = 0;
    bool lhsKnown =
        ExtractParamOrLiteral(BO->getLHS(), ParamName, ParamValue, lhsVal);
    bool rhsKnown =
        ExtractParamOrLiteral(BO->getRHS(), ParamName, ParamValue, rhsVal);
    if (!lhsKnown || !rhsKnown)
      return EvalResult::Unknown;

    switch (BO->getOpcode()) {
    case BO_EQ:
      return lhsVal == rhsVal ? EvalResult::True : EvalResult::False;
    case BO_NE:
      return lhsVal != rhsVal ? EvalResult::True : EvalResult::False;
    case BO_LT:
      return lhsVal < rhsVal ? EvalResult::True : EvalResult::False;
    case BO_GT:
      return lhsVal > rhsVal ? EvalResult::True : EvalResult::False;
    case BO_LE:
      return lhsVal <= rhsVal ? EvalResult::True : EvalResult::False;
    case BO_GE:
      return lhsVal >= rhsVal ? EvalResult::True : EvalResult::False;
    default:
      return EvalResult::Unknown;
    }
  }

  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_LNot) {
      auto R = EvalConditionForParam(UO->getSubExpr(), ParamName, ParamValue);
      if (R == EvalResult::True)
        return EvalResult::False;
      if (R == EvalResult::False)
        return EvalResult::True;
      return EvalResult::Unknown;
    }
  }

  return EvalResult::Unknown;
}

} // anonymous namespace

class TuplingRule : public TransformationRule {
public:
  bool applies(const FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override {
    if (Ctx.ParamNames.empty())
      return false;

    std::vector<LinearTerm> terms;
    int maxOrder = 0;
    if (!ParseLinearTerms(BA.RecExpr, Ctx.FuncName, Ctx.ParamNames[0], terms,
                          maxOrder))
      return false;
    if (maxOrder < 2)
      return false;

    // Every order 1..maxOrder must appear exactly once.
    std::vector<int> orderCount(maxOrder + 1, 0);
    for (const auto &t : terms) {
      if (t.Order > maxOrder)
        return false;
      orderCount[t.Order]++;
    }
    for (int c = 1; c <= maxOrder; ++c) {
      if (orderCount[c] != 1)
        return false;
    }

    // Base cases must cover parameter values 0..maxOrder-1.
    for (int j = 0; j < maxOrder; ++j) {
      bool covered = false;
      for (const auto &bc : BA.BaseCases) {
        auto r = EvalConditionForParam(bc.first, Ctx.ParamNames[0], j);
        if (r == EvalResult::True) {
          covered = true;
          break;
        }
        if (r == EvalResult::Unknown)
          return false;
      }
      if (!covered)
        return false;
    }

    return true;
  }

  std::string apply(const FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override {
    std::string pName = Ctx.ParamNames[0];
    std::string pType = GetParamStorageType(FD->getParamDecl(0));

    std::vector<LinearTerm> terms;
    int maxOrder = 0;
    ParseLinearTerms(BA.RecExpr, Ctx.FuncName, pName, terms, maxOrder);
    int k = maxOrder;

    std::unordered_map<const Expr *, std::string> repls;
    for (const auto &t : terms)
      repls[t.Hole] = "vals[" + std::to_string(k - t.Order) + "]";
    std::string nextExpr =
        PrintExprWithReplacements(BA.RecExpr, repls, Ctx.ASTCtx);

    CodeEmitter e;
    e.raw("// === Generated tupling code for function: " + Ctx.FuncName +
          " ===\n\n");
    e.line("#include <array>");
    e.nl();

    std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
    e.block(sig, [&](CodeEmitter &b) {
      // Early base-case return.
      b.block("if (" + pName + " <= " + std::to_string(k - 1) + ")",
              [&](CodeEmitter &iw) {
                for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
                  std::string prefix = (bi == 0) ? "if (" : "else if (";
                  iw.line(prefix + PrintExpr(BA.BaseCases[bi].first, Ctx.ASTCtx) +
                          ") return " +
                          PrintExpr(BA.BaseCases[bi].second, Ctx.ASTCtx) + ";");
                }
                iw.line("return 0;");
              });

      b.line("std::array<" + Ctx.RetType + ", " + std::to_string(k) + "> vals;");

      for (int j = 0; j < k; ++j) {
        b.line("{");
        b.inc();
        b.line(pType + " " + pName + " = " + std::to_string(j) + ";");
        for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
          std::string prefix = (bi == 0) ? "if (" : "else if (";
          b.line(prefix + PrintExpr(BA.BaseCases[bi].first, Ctx.ASTCtx) + ")");
          b.inc();
          b.line("vals[" + std::to_string(j) + "] = " +
                 PrintExpr(BA.BaseCases[bi].second, Ctx.ASTCtx) + ";");
          b.dec();
        }
        b.line("else vals[" + std::to_string(j) + "] = 0;");
        b.dec();
        b.line("}");
      }

      b.block("for (" + pType + " i = " + std::to_string(k) + "; i <= " +
                  pName + "; ++i)",
              [&](CodeEmitter &fw) {
                fw.line(Ctx.RetType + " next = " + nextExpr + ";");
                fw.line("for (int j = 0; j < " + std::to_string(k - 1) +
                        "; ++j)");
                fw.inc();
                fw.line("vals[j] = vals[j + 1];");
                fw.dec();
                fw.line("vals[" + std::to_string(k - 1) + "] = next;");
              });
      b.line("return vals[" + std::to_string(k - 1) + "];");
    });

    return e.str();
  }

  const char *name() const override { return "TuplingRule"; }
};

// ============================================================
// Binary stack rule (+, *, |, ^)
// ============================================================

namespace {

bool ExtractTwoRecursiveCalls(const Expr *LHS, const Expr *RHS,
                              const std::string &FuncName,
                              std::vector<std::string> &LeftCallArgs,
                              std::vector<std::string> &RightCallArgs,
                              const ASTContext *Ctx) {
  const CallExpr *LeftCall = dyn_cast<CallExpr>(LHS);
  const CallExpr *RightCall = dyn_cast<CallExpr>(RHS);
  if (!LeftCall || !RightCall)
    return false;

  const FunctionDecl *LeftCallee = LeftCall->getDirectCallee();
  const FunctionDecl *RightCallee = RightCall->getDirectCallee();
  if (!LeftCallee || !RightCallee)
    return false;
  if (LeftCallee->getNameAsString() != FuncName ||
      RightCallee->getNameAsString() != FuncName)
    return false;

  for (unsigned i = 0; i < LeftCall->getNumArgs(); ++i)
    LeftCallArgs.push_back(PrintExpr(LeftCall->getArg(i), Ctx));
  for (unsigned i = 0; i < RightCall->getNumArgs(); ++i)
    RightCallArgs.push_back(PrintExpr(RightCall->getArg(i), Ctx));
  return true;
}

} // anonymous namespace

class BinaryStackRule : public TransformationRule {
public:
  bool applies(const FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override {
    const Expr *E = BA.RecExpr->IgnoreParenImpCasts();
    const BinaryOperator *BO = dyn_cast<BinaryOperator>(E);
    if (!BO)
      return false;
    std::string op = BO->getOpcodeStr().str();
    if (op != "+" && op != "*" && op != "|" && op != "^")
      return false;

    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
    std::vector<std::string> leftArgs, rightArgs;
    return ExtractTwoRecursiveCalls(LHS, RHS, Ctx.FuncName, leftArgs,
                                    rightArgs, Ctx.ASTCtx);
  }

  std::string apply(const FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override {
    const BinaryOperator *BO =
        dyn_cast<BinaryOperator>(BA.RecExpr->IgnoreParenImpCasts());
    std::string op = BO->getOpcodeStr().str();

    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
    std::vector<std::string> leftArgs, rightArgs;
    ExtractTwoRecursiveCalls(LHS, RHS, Ctx.FuncName, leftArgs, rightArgs,
                             Ctx.ASTCtx);

    std::string identity;
    std::string combine;
    if (op == "+") {
      identity = "0";
      combine = "result += ";
    } else if (op == "*") {
      identity = "1";
      combine = "result *= ";
    } else if (op == "|") {
      identity = "0";
      combine = "result |= ";
    } else { // ^
      identity = "0";
      combine = "result ^= ";
    }

    CodeEmitter e;
    e.raw("// === Generated binary-stack code for function: " + Ctx.FuncName +
          " ===\n\n");
    e.line("#include <vector>");
    e.nl();

    std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
    std::string frameName = EmitFrameStruct(e, FD, Ctx);

    e.block(sig, [&](CodeEmitter &b) {
      b.line("std::vector<" + frameName + "> stack;");
      {
        std::string init = "stack.emplace_back(";
        for (unsigned i = 0; i < FD->getNumParams(); ++i) {
          if (i > 0)
            init += ", ";
          init += FD->getParamDecl(i)->getNameAsString();
        }
        init += ");";
        b.line(init);
      }
      b.line(Ctx.RetType + " result = " + identity + ";");
      b.block("while (!stack.empty())", [&](CodeEmitter &w) {
        w.line("auto cur = stack.back();");
        w.line("stack.pop_back();");
        EmitStmts(w, BA.LeadingStmts, Ctx.ASTCtx);
        for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
          std::string prefix = (bi == 0) ? "if (" : "else if (";
          const auto &bc = BA.BaseCases[bi];
          w.line(prefix + ReplaceParamsWithCur(PrintExpr(bc.first, Ctx.ASTCtx),
                                               Ctx.ParamNames) +
                 ") {");
          w.inc();
          w.line(combine +
                 ReplaceParamsWithCur(PrintExpr(bc.second, Ctx.ASTCtx),
                                      Ctx.ParamNames) +
                 ";");
          w.dec();
          w.line("}");
        }
        w.line("else {");
        w.inc();
        EmitStmts(w, BA.MiddleStmts, Ctx.ASTCtx);
        {
          std::string push = "stack.emplace_back(";
          for (unsigned i = 0;
               i < FD->getNumParams() && i < rightArgs.size(); ++i) {
            if (i > 0)
              push += ", ";
            push += ReplaceParamsWithCur(rightArgs[i], Ctx.ParamNames);
          }
          push += ");";
          w.line(push);
        }
        {
          std::string push = "stack.emplace_back(";
          for (unsigned i = 0;
               i < FD->getNumParams() && i < leftArgs.size(); ++i) {
            if (i > 0)
              push += ", ";
            push += ReplaceParamsWithCur(leftArgs[i], Ctx.ParamNames);
          }
          push += ");";
          w.line(push);
        }
        w.dec();
        w.line("}");
      });
      b.line("return result;");
    });

    return e.str();
  }

  const char *name() const override { return "BinaryStackRule"; }
};

// ============================================================
// Generic stack rule (explicit value stack for arbitrary rec expr)
// ============================================================

class GenericStackRule : public TransformationRule {
public:
  bool applies(const FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override {
    std::vector<CallExpr *> holes;
    CollectHoles(BA.RecExpr, Ctx.FuncName, holes);
    return !holes.empty();
  }

  std::string apply(const FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override {
    std::vector<CallExpr *> holes;
    CollectHoles(BA.RecExpr, Ctx.FuncName, holes);

    bool needsAlgorithm = false;
    std::string combinedExpr;
    if (const CallExpr *CE = dyn_cast<CallExpr>(BA.RecExpr)) {
      if (const FunctionDecl *Callee = CE->getDirectCallee()) {
        std::string name = Callee->getNameAsString();
        if ((name == "min" || name == "max") && CE->getNumArgs() == 2) {
          needsAlgorithm = true;
          std::unordered_map<const Expr *, std::string> repls;
          for (size_t i = 0; i < holes.size(); ++i)
            repls[holes[i]] = "v" + std::to_string(i);
          std::string a0 =
              PrintExprWithReplacements(CE->getArg(0), repls, Ctx.ASTCtx);
          std::string a1 =
              PrintExprWithReplacements(CE->getArg(1), repls, Ctx.ASTCtx);
          combinedExpr = "std::" + name + "(" + a0 + ", " + a1 + ")";
        }
      }
    }
    if (combinedExpr.empty()) {
      std::unordered_map<const Expr *, std::string> repls;
      for (size_t i = 0; i < holes.size(); ++i)
        repls[holes[i]] = "v" + std::to_string(i);
      combinedExpr = PrintExprWithReplacements(BA.RecExpr, repls, Ctx.ASTCtx);
    }

    CodeEmitter e;
    e.raw("// === Generated generic-stack code for function: " + Ctx.FuncName +
          " ===\n\n");
    e.line("#include <vector>");
    e.line("#include <variant>");
    if (needsAlgorithm)
      e.line("#include <algorithm>");
    e.nl();

    std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
    std::string frameName = EmitFrameStruct(e, FD, Ctx);

    e.block(sig, [&](CodeEmitter &b) {
      b.line("std::vector<std::variant<" + frameName + ", int>> stack;");
      {
        std::string init = "stack.emplace_back(" + frameName + "(";
        for (unsigned i = 0; i < FD->getNumParams(); ++i) {
          if (i > 0)
            init += ", ";
          init += FD->getParamDecl(i)->getNameAsString();
        }
        init += "));";
        b.line(init);
      }
      b.line("std::vector<" + Ctx.RetType + "> values;");

      b.block("while (!stack.empty())", [&](CodeEmitter &w) {
        w.line("auto entry = stack.back();");
        w.line("stack.pop_back();");
        w.block("if (std::holds_alternative<int>(entry))", [&](CodeEmitter &iw) {
          for (size_t i = 0; i < holes.size(); ++i) {
            iw.line(Ctx.RetType + " v" + std::to_string(i) +
                    " = values.back();");
            iw.line("values.pop_back();");
          }
          iw.line("values.push_back(" + combinedExpr + ");");
        });
        w.block("else", [&](CodeEmitter &iw) {
          iw.line("auto cur = std::get<" + frameName + ">(entry);");
          for (const auto &p : Ctx.ParamNames)
            iw.line("auto " + p + " = cur." + p + ";");
          EmitStmts(iw, BA.LeadingStmts, Ctx.ASTCtx);
          for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
            std::string prefix = (bi == 0) ? "if (" : "else if (";
            const auto &bc = BA.BaseCases[bi];
            iw.line(prefix + PrintExpr(bc.first, Ctx.ASTCtx) + ")");
            iw.inc();
            iw.line("values.push_back(" + PrintExpr(bc.second, Ctx.ASTCtx) +
                    ");");
            iw.dec();
          }
          iw.line("else {");
          iw.inc();
          EmitStmts(iw, BA.MiddleStmts, Ctx.ASTCtx);
          iw.line("stack.emplace_back(" + std::to_string(holes.size()) + ");");
          for (size_t i = 0; i < holes.size(); ++i) {
            std::string push = "stack.emplace_back(" + frameName + "(";
            for (unsigned a = 0;
                 a < FD->getNumParams() && a < holes[i]->getNumArgs(); ++a) {
              if (a > 0)
                push += ", ";
              push += PrintExpr(holes[i]->getArg(a), Ctx.ASTCtx);
            }
            push += "));";
            iw.line(push);
          }
          iw.dec();
          iw.line("}");
        });
      });
      b.line("return values.back();");
    });

    return e.str();
  }

  const char *name() const override { return "GenericStackRule"; }
};

// ============================================================
// Defunctionalized fallback rule
// ============================================================

namespace {

struct DefunClosureInfo {
  std::string Name;
  bool NeedsSavedArg = false;
};

} // anonymous namespace

class DefunctionalizedRule : public TransformationRule {
public:
  bool applies(const FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override {
    return BA.IsRecursive;
  }

  std::string apply(const FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override {
    const Expr *RecExpr = BA.RecExpr;

    CodeEmitter e;
    e.raw("// === Generated defunctionalized code for function: " +
          Ctx.FuncName + " ===\n\n");
    e.line("#include <vector>");
    e.nl();

    std::string sig = BuildFunctionSignature(FD, Ctx.RetType);

    // Arg struct.
    {
      std::string header = "struct " + Ctx.ArgType;
      e.block(header, [&](CodeEmitter &b) {
        for (unsigned i = 0; i < FD->getNumParams(); ++i) {
          b.line(GetParamStorageType(FD->getParamDecl(i)) + " " +
                 FD->getParamDecl(i)->getNameAsString() + ";");
        }
        std::string ctor = Ctx.ArgType + "(";
        for (unsigned i = 0; i < FD->getNumParams(); ++i) {
          if (i > 0)
            ctor += ", ";
          ctor += GetParamStorageType(FD->getParamDecl(i)) + " " +
                  FD->getParamDecl(i)->getNameAsString();
        }
        ctor += ") : ";
        for (unsigned i = 0; i < FD->getNumParams(); ++i) {
          if (i > 0)
            ctor += ", ";
          std::string pName = FD->getParamDecl(i)->getNameAsString();
          ctor += pName + "(" + pName + ")";
        }
        ctor += " {}";
        b.line(ctor);
        {
          std::string dtor = Ctx.ArgType + "() : ";
          bool first = true;
          for (unsigned i = 0; i < FD->getNumParams(); ++i) {
            if (!first)
              dtor += ", ";
            std::string pName = FD->getParamDecl(i)->getNameAsString();
            dtor += pName + "(0)";
            first = false;
          }
          dtor += " {}";
          b.line(dtor);
        }
      }, ";");
    }
    e.nl();

    std::vector<CallExpr *> holes;
    CollectHoles(RecExpr, Ctx.FuncName, holes);

    if (holes.size() == 1 && holes[0] == RecExpr->IgnoreParenImpCasts()) {
      e.block(sig, [&](CodeEmitter &b) {
        b.line(Ctx.ArgType + " arg = " +
               ArgCtorDefun(std::vector<std::string>(Ctx.ParamNames.size(), "0"),
                            Ctx) +
               ";");
        for (unsigned i = 0; i < FD->getNumParams(); ++i) {
          b.line("arg." + Ctx.ParamNames[i] + " = " +
                 FD->getParamDecl(i)->getNameAsString() + ";");
        }
        b.block("while (1)", [&](CodeEmitter &w) {
          EmitUnpacksDefun(w, "arg", Ctx);
          EmitStmts(w, BA.LeadingStmts, Ctx.ASTCtx);
          for (const auto &bc : BA.BaseCases) {
            w.line("if (" + PrintExpr(bc.first, Ctx.ASTCtx) + ") return " +
                   PrintExpr(bc.second, Ctx.ASTCtx) + ";");
          }
          EmitStmts(w, BA.MiddleStmts, Ctx.ASTCtx);
          if (const CallExpr *RecCall = dyn_cast<CallExpr>(RecExpr)) {
            for (unsigned i = 0;
                 i < FD->getNumParams() && i < RecCall->getNumArgs(); ++i) {
              std::string pName = FD->getParamDecl(i)->getNameAsString();
              w.line("arg." + pName + " = " +
                     PrintExpr(RecCall->getArg(i), Ctx.ASTCtx) + ";");
            }
          }
        });
      });
      return e.str();
    }

    std::vector<DefunClosureInfo> closures;
    for (size_t i = 0; i < holes.size(); ++i) {
      closures.push_back(
          {"K" + std::to_string(i),
           NeedsSavedArg(RecExpr, holes, i, Ctx.ParamNameSet)});
    }

    {
      e.raw("enum class " + Ctx.FuncName + "Cont {\n");
      e.raw("  Done,\n");
      for (const auto &c : closures)
        e.raw("  " + c.Name + ",\n");
      e.raw("};\n");
    }
    e.nl();

    {
      std::string frameName = Ctx.FuncName + "Frame";
      e.block("struct " + frameName, [&](CodeEmitter &b) {
        b.line(Ctx.FuncName + "Cont tag;");
        b.line("std::vector<" + Ctx.RetType + "> vals;");
        b.line("bool has_arg;");
        b.line(Ctx.ArgType + " saved_arg;");
        b.line(frameName + "(" + Ctx.FuncName +
               "Cont t) : tag(t), has_arg(false) {}");
        b.line(frameName + "(" + Ctx.FuncName +
               "Cont t, std::vector<" + Ctx.RetType +
               "> v) : tag(t), vals(std::move(v)), has_arg(false) {}");
        b.line(frameName + "(" + Ctx.FuncName +
               "Cont t, std::vector<" + Ctx.RetType + "> v, " + Ctx.ArgType +
               " a) : tag(t), vals(std::move(v)), has_arg(true), saved_arg(a) "
               "{}");
      }, ";");
    }
    e.nl();

    CodeEmitter casesEmitter;
    casesEmitter.line("case " + Ctx.FuncName + "Cont::Done:");
    casesEmitter.inc();
    casesEmitter.line("return val;");
    casesEmitter.dec();
    for (size_t i = 0; i < closures.size(); ++i) {
      const auto &cls = closures[i];
      casesEmitter.line("case " + Ctx.FuncName + "Cont::" + cls.Name + ": {");
      casesEmitter.inc();
      if (cls.NeedsSavedArg)
        EmitUnpacksDefun(casesEmitter, "f.saved_arg", Ctx);
      if (i == closures.size() - 1) {
        std::unordered_map<const Expr *, std::string> repls;
        for (size_t j = 0; j < holes.size(); ++j)
          repls[holes[j]] =
              (j == i) ? "val" : "f.vals[" + std::to_string(j) + "]";
        std::string finalExpr =
            PrintExprWithReplacements(RecExpr, repls, Ctx.ASTCtx);
        casesEmitter.line("val = " + finalExpr + ";");
        casesEmitter.line("break;");
      } else {
        std::vector<std::string> captured;
        for (size_t j = 0; j < i; ++j)
          captured.push_back("f.vals[" + std::to_string(j) + "]");
        captured.push_back("val");
        std::string push = "k.emplace_back(" + Ctx.FuncName + "Cont::K" +
                           std::to_string(i + 1) + ", ";
        push += "std::vector<" + Ctx.RetType + ">{";
        for (size_t j = 0; j < captured.size(); ++j) {
          if (j > 0)
            push += ", ";
          push += captured[j];
        }
        push += "}";
        if (closures[i + 1].NeedsSavedArg)
          push += ", f.saved_arg";
        push += ");";
        casesEmitter.line(push);

        std::vector<std::string> newParams;
        for (unsigned a = 0;
             a < FD->getNumParams() && a < holes[i + 1]->getNumArgs(); ++a)
          newParams.push_back(PrintExpr(holes[i + 1]->getArg(a), Ctx.ASTCtx));
        casesEmitter.line("arg = " + ArgCtorDefun(newParams, Ctx) + ";");
        casesEmitter.line("goto dispatch;");
      }
      casesEmitter.dec();
      casesEmitter.line("}");
    }

    std::string initialSetup;
    {
      std::string push = "k.emplace_back(" + Ctx.FuncName + "Cont::K0";
      if (closures[0].NeedsSavedArg)
        push += ", std::vector<" + Ctx.RetType + ">{}, arg";
      else
        push += ", std::vector<" + Ctx.RetType + ">{}";
      push += ");";
      initialSetup += push + "\n";
      std::vector<std::string> newParams;
      for (unsigned a = 0;
           a < FD->getNumParams() && a < holes[0]->getNumArgs(); ++a)
        newParams.push_back(PrintExpr(holes[0]->getArg(a), Ctx.ASTCtx));
      initialSetup += "arg = " + ArgCtorDefun(newParams, Ctx) + ";";
    }

    e.block(sig, [&](CodeEmitter &b) {
      b.line("std::vector<" + Ctx.FuncName + "Frame> k;");
      b.line("k.emplace_back(" + Ctx.FuncName + "Cont::Done);");
      b.line(Ctx.ArgType + " arg = " +
             ArgCtorDefun(std::vector<std::string>(Ctx.ParamNames.size(), "0"),
                          Ctx) +
             ";");
      for (unsigned i = 0; i < FD->getNumParams(); ++i) {
        b.line("arg." + Ctx.ParamNames[i] + " = " +
               FD->getParamDecl(i)->getNameAsString() + ";");
      }
      b.line(Ctx.RetType + " val = 0;");
      b.line("dispatch:");
      b.block("while (1)", [&](CodeEmitter &w) {
        EmitUnpacksDefun(w, "arg", Ctx);
        EmitStmts(w, BA.LeadingStmts, Ctx.ASTCtx);
        for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
          std::string prefix = (bi == 0) ? "if (" : "else if (";
          const auto &bc = BA.BaseCases[bi];
          w.block(prefix + PrintExpr(bc.first, Ctx.ASTCtx) + ")",
                  [&](CodeEmitter &iw) {
                    iw.line("val = " + PrintExpr(bc.second, Ctx.ASTCtx) + ";");
                  });
        }
        w.line("else {");
        w.inc();
        EmitStmts(w, BA.MiddleStmts, Ctx.ASTCtx);
        w.raw(Indent(initialSetup, 6));
        w.nl();
        w.line("goto dispatch;");
        w.dec();
        w.line("}");
        w.block("while (!k.empty())", [&](CodeEmitter &iw) {
          iw.line("auto f = k.back();");
          iw.line("k.pop_back();");
          iw.block("switch (f.tag)", [&](CodeEmitter &sw) {
            sw.raw(Indent(casesEmitter.str(), 6));
            sw.nl();
          });
        });
        w.line("return val;");
      });
    });

    return e.str();
  }

  const char *name() const override { return "DefunctionalizedRule"; }
};

} // anonymous namespace

// ============================================================
// Rule factory
// ============================================================

std::vector<std::unique_ptr<TransformationRule>> CreateDefaultRules() {
  std::vector<std::unique_ptr<TransformationRule>> rules;
  rules.emplace_back(std::make_unique<TailRecursionRule>());
  rules.emplace_back(std::make_unique<AccumulatorRule>());
  rules.emplace_back(std::make_unique<TuplingRule>());
  rules.emplace_back(std::make_unique<BinaryStackRule>());
  rules.emplace_back(std::make_unique<GenericStackRule>());
  rules.emplace_back(std::make_unique<DefunctionalizedRule>());
  return rules;
}

} // namespace cps
