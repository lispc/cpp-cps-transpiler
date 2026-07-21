#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "output_ir.h"
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

  const BaseCaseRename curRename = MakeCurRename(Ctx, "__cps_cur");
  auto replaceCurCond = [&](const BaseCase &bc) -> std::string {
    return PrintBaseCaseCond(bc, Ctx.ASTCtx, curRename);
  };

  auto replaceCurValue = [&](const BaseCase &bc) -> std::string {
    return PrintBaseCaseValue(bc, Ctx.ASTCtx, curRename);
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

  IRBuilder b;
  StackMachineCodegen smg(Ctx.FuncName, Ctx.RetType);
  smg.emitBanner(b, "binary-stack", Ctx.FuncName);
  smg.emitIncludes(b);

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);

  for (unsigned i = 0; i < FD->getNumParams(); ++i)
    smg.addParam(FD->getParamDecl(i));
  smg.emitFrameStruct(b);

  auto body = IRBuilder::block();
  IRBuilder::add(
      body.get(),
      IRBuilder::var("std::vector<" + smg.frameName() + ">", smg.stackName()));
  IRBuilder::add(body.get(),
                 IRBuilder::expr(IRExpr(smg.stackName() + ".emplace_back(" +
                                        smg.frameName() + "(" +
                                        buildInitArgs() + "))")));
  IRBuilder::add(body.get(),
                 IRBuilder::var(Ctx.RetType, "result", IRExpr(identity)));

  smg.emitSimpleLoop(body.get(), [&](IRBlock *w) {
    EmitStmtsToIR(b, w, BA.LeadingStmts, Ctx.ASTCtx);

    // else branch: middle statements + push both recursive calls.
    auto elseBlk = IRBuilder::block();
    EmitStmtsToIR(b, elseBlk.get(), BA.MiddleStmts, Ctx.ASTCtx);
    IRBuilder::add(elseBlk.get(),
                   IRBuilder::expr(IRExpr(smg.stackName() + ".emplace_back(" +
                                          smg.frameName() + "(" +
                                          buildPushArgs(RightCall) + "))")));
    IRBuilder::add(elseBlk.get(),
                   IRBuilder::expr(IRExpr(smg.stackName() + ".emplace_back(" +
                                          smg.frameName() + "(" +
                                          buildPushArgs(LeftCall) + "))")));

    // Fold the base cases into an if / else-if / else chain.
    std::unique_ptr<IRStmt> chain = std::move(elseBlk);
    for (size_t bi = BA.BaseCases.size(); bi-- > 0;) {
      const auto &bc = BA.BaseCases[bi];
      auto thenBlk = IRBuilder::block();
      IRBuilder::add(thenBlk.get(),
                     IRBuilder::expr(IRExpr(combine + replaceCurValue(bc))));
      chain = IRBuilder::if_(IRExpr(replaceCurCond(bc)), std::move(thenBlk),
                             std::move(chain));
    }
    IRBuilder::add(w, std::move(chain));
  });

  IRBuilder::add(body.get(), IRBuilder::ret(IRExpr("result")));
  b.function(sig, std::move(body));

  return PrintGeneratedUnit(b.unit);
}

} // namespace cps
