#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "code_emitter.h"
#include "output_ir.h"
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

  IRBuilder b;
  b.raw("// === Generated accumulator code for function: " + Ctx.FuncName +
        " ===\n\n");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  auto body = IRBuilder::block();

  EmitStmtsToIR(b, body.get(), BA.LeadingStmts, Ctx.ASTCtx);
  IRBuilder::add(body.get(),
                 IRBuilder::var(Ctx.RetType, accName,
                                IRExpr(BA.BaseCases[0].ValueStr)));

  auto loopBody = IRBuilder::block();
  EmitStmtsToIR(b, loopBody.get(), BA.MiddleStmts, Ctx.ASTCtx);

  std::string stepExpr = StripOuterParens(PrintExpr(Step, Ctx.ASTCtx));
  if (!op.empty()) {
    IRBuilder::add(loopBody.get(),
                   IRBuilder::expr(
                       IRExpr(accName + " = " + accName + " " + op + " " +
                              stepExpr)));
  } else {
    IRBuilder::add(loopBody.get(),
                   IRBuilder::expr(IRExpr(accName + " = " + funcName + "(" +
                                          accName + ", " + stepExpr + ")")));
  }

  {
    CodeEmitter tmp;
    EmitTailRecParamUpdate(tmp, FD, dyn_cast<CallExpr>(RecCall), Ctx.ASTCtx);
    std::istringstream iss(tmp.str());
    std::string line;
    while (std::getline(iss, line)) {
      if (!line.empty())
        IRBuilder::add(loopBody.get(), IRBuilder::rawStmt(line));
    }
  }

  IRBuilder::add(body.get(),
                 IRBuilder::while_(IRExpr(loopCond), std::move(loopBody)));
  IRBuilder::add(body.get(), IRBuilder::ret(IRExpr(accName)));
  b.function(sig, std::move(body));

  return PrintGeneratedUnit(b.unit);
}

int AccumulatorRule::cost() const { return RuleCatalog::Accumulator.Cost; }

const char *AccumulatorRule::name() const {
  return RuleCatalog::Accumulator.Name;
}

} // namespace cps
