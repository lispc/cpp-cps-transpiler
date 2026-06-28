#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "code_emitter.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <string>
#include <vector>

namespace cps {

using namespace clang;

bool AccumulatorRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                              const GenContext &Ctx) const {
  if (Ctx.RetType == "void" || !BA.RecExpr)
    return false;
  // All base-case return values must be identical and parameter-free, so
  // they can serve as the accumulator identity.
  if (BA.BaseCases.empty())
    return false;
  std::string baseValue;
  for (size_t i = 0; i < BA.BaseCases.size(); ++i) {
    if (!BA.BaseCases[i].ValueExpr ||
        ExprUsesParams(BA.BaseCases[i].ValueExpr, Ctx.ParamDeclSet))
      return false;
    std::string val = BA.BaseCases[i].ValueStr;
    if (i == 0)
      baseValue = val;
    else if (val != baseValue)
      return false;
  }

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
    if (op != "+" && op != "-" && op != "*" && op != "|" && op != "^")
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
  if (!((lhsRec && !rhsRec) || (!lhsRec && rhsRec)))
    return false;

  const Expr *Step = lhsRec ? RHS : LHS;
  for (size_t i = 0; i < BA.BaseCases.size(); ++i) {
    if (!BA.BaseCases[i].ValueExpr || !IsPureExpr(BA.BaseCases[i].ValueExpr))
      return false;
  }
  return IsPureExpr(Step);
}

CpsResult AccumulatorRule::apply(const FunctionDecl *FD,
                                   const BodyAnalysis &BA,
                                   GenContext &Ctx) const {
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
  EmitGeneratedBanner(e, "accumulator", Ctx.FuncName);

  std::string accName = "acc";
  if (!op.empty()) {
    if (op == "+") accName = "sum";
    else if (op == "-") accName = "diff";
    else if (op == "*") accName = "product";
    else if (op == "|") accName = "bits";
    else if (op == "^") accName = "xors";
  } else {
    if (funcName == "min") accName = "min_val";
    else if (funcName == "max") accName = "max_val";
  }

  std::string loopCond;
  for (size_t i = 0; i < BA.BaseCases.size(); ++i) {
    if (i > 0)
      loopCond += " && ";
    loopCond += "!(" + BA.BaseCases[i].CondStr + ")";
  }

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  e.block(sig, [&](CodeEmitter &b) {
    EmitStmts(b, BA.LeadingStmts, Ctx.ASTCtx);
    b.line(Ctx.RetType + " " + accName + " = " +
           BA.BaseCases[0].ValueStr + ";");
    b.block("while (" + loopCond + ")",
            [&](CodeEmitter &w) {
              EmitStmts(w, BA.MiddleStmts, Ctx.ASTCtx);
              if (!op.empty()) {
                w.line(accName + " = " + accName + " " + op + " " +
                       StripOuterParens(PrintExpr(Step, Ctx.ASTCtx)) + ";");
              } else {
                w.line(accName + " = " + funcName + "(" + accName + ", " +
                       StripOuterParens(PrintExpr(Step, Ctx.ASTCtx)) + ");");
              }
              EmitTailRecParamUpdate(w, FD, dyn_cast<CallExpr>(RecCall),
                                     Ctx.ASTCtx);
            });
    b.line("return " + accName + ";");
  });

  return e.str();
}

int AccumulatorRule::cost() const { return RuleCatalog::Accumulator.Cost; }

const char *AccumulatorRule::name() const {
  return RuleCatalog::Accumulator.Name;
}

} // namespace cps
