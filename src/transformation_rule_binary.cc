#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "code_emitter.h"
#include "stack_machine_codegen.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <string>
#include <vector>

namespace cps {

using namespace clang;

namespace {

bool ExtractTwoRecursiveCalls(const Expr *LHS, const Expr *RHS,
                              const std::string &FuncName,
                              const CallExpr *&OutLeftCall,
                              const CallExpr *&OutRightCall) {
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

  OutLeftCall = LeftCall;
  OutRightCall = RightCall;
  return true;
}

} // anonymous namespace

bool BinaryStackRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                              const GenContext &Ctx) const {
  if (Ctx.RetType == "void" || !BA.RecExpr)
    return false;
  const Expr *E = BA.RecExpr->IgnoreParenImpCasts();
  const BinaryOperator *BO = dyn_cast<BinaryOperator>(E);
  if (!BO)
    return false;
  std::string op = BO->getOpcodeStr().str();
  // && / || are not handled here because BinaryStackRule evaluates both
  // sub-trees before combining, which breaks short-circuit semantics.
  if (op != "+" && op != "*" && op != "|" && op != "^")
    return false;

  const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
  const CallExpr *LeftCall = nullptr;
  const CallExpr *RightCall = nullptr;
  return ExtractTwoRecursiveCalls(LHS, RHS, Ctx.FuncName, LeftCall,
                                  RightCall);
}

CpsResult BinaryStackRule::apply(const FunctionDecl *FD,
                                   const BodyAnalysis &BA,
                                   GenContext &Ctx) const {
  const BinaryOperator *BO =
      dyn_cast<BinaryOperator>(BA.RecExpr->IgnoreParenImpCasts());
  std::string op = BO->getOpcodeStr().str();

  const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
  const CallExpr *LeftCall = nullptr;
  const CallExpr *RightCall = nullptr;
  ExtractTwoRecursiveCalls(LHS, RHS, Ctx.FuncName, LeftCall, RightCall);

  auto replaceCurCond = [&](const BaseCase &bc) -> std::string {
    if (bc.CondExpr)
      return ReplaceParamsWithCur(bc.CondExpr, Ctx);
    return ReplaceParamsWithCurInString(bc.CondStr, Ctx.ParamNames);
  };

  auto replaceCurValue = [&](const BaseCase &bc) -> std::string {
    if (bc.ValueExpr)
      return ReplaceParamsWithCur(bc.ValueExpr, Ctx);
    return ReplaceParamsWithCurInString(bc.ValueStr, Ctx.ParamNames);
  };

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

  auto buildInitArgs = [&]() {
    std::string s;
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (i > 0)
        s += ", ";
      s += FD->getParamDecl(i)->getNameAsString();
    }
    return s;
  };

  auto buildPushArgs = [&](const CallExpr *RecCall) {
    std::string s;
    for (unsigned i = 0;
         i < FD->getNumParams() && i < RecCall->getNumArgs(); ++i) {
      if (i > 0)
        s += ", ";
      s += ReplaceParamsWithCur(RecCall->getArg(i), Ctx);
    }
    return s;
  };

  CodeEmitter e;
  StackMachineCodegen smg(e, Ctx.FuncName, Ctx.RetType);
  smg.emitBanner("binary-stack", Ctx.FuncName);
  smg.emitIncludes();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);

  for (unsigned i = 0; i < FD->getNumParams(); ++i)
    smg.addParam(FD->getParamDecl(i));
  smg.emitFrameStruct();

  e.block(sig, [&](CodeEmitter &b) {
    b.line("std::vector<" + smg.frameName() + "> " + smg.stackName() + ";");
    b.line(smg.stackName() + ".emplace_back(" + smg.frameName() + "(" +
           buildInitArgs() + "));");
    b.line(Ctx.RetType + " result = " + identity + ";");
    smg.emitSimpleLoop([&](CodeEmitter &w) {
      EmitStmts(w, BA.LeadingStmts, Ctx.ASTCtx);
      for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
        std::string prefix = (bi == 0) ? "if (" : "else if (";
        const auto &bc = BA.BaseCases[bi];
        w.line(prefix + replaceCurCond(bc) + ") {");
        w.inc();
        w.line(combine + replaceCurValue(bc) + ";");
        w.dec();
        w.line("}");
      }
      w.line("else {");
      w.inc();
      EmitStmts(w, BA.MiddleStmts, Ctx.ASTCtx);
      w.line(smg.stackName() + ".emplace_back(" + smg.frameName() + "(" +
             buildPushArgs(RightCall) + "));");
      w.line(smg.stackName() + ".emplace_back(" + smg.frameName() + "(" +
             buildPushArgs(LeftCall) + "));");
      w.dec();
      w.line("}");
    });
    b.line("return result;");
  });

  return e.str();
}

int BinaryStackRule::cost() const { return RuleCatalog::BinaryStack.Cost; }

const char *BinaryStackRule::name() const {
  return RuleCatalog::BinaryStack.Name;
}

} // namespace cps
