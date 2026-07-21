#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "output_ir.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <functional>
#include <string>
#include <vector>

namespace cps {

using namespace clang;

bool TailRecursionRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                                const GenContext &Ctx) const {
  // apply() consumes BA (base cases, middle statements, the recursive call).
  // When AnalyzeBody failed, BA is empty and apply() would silently emit an
  // infinite loop with no base case and no parameter update.
  if (!BA.IsRecursive)
    return false;

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

CpsResult TailRecursionRule::apply(const FunctionDecl *FD,
                                     const BodyAnalysis &BA,
                                     GenContext &Ctx) const {
  IRBuilder b;
  b.comment("=== Generated tail-recursion optimized code for function: " +
            Ctx.FuncName + " ===");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  auto body = IRBuilder::block();
  auto loopBody = IRBuilder::block();

  EmitStmtsToIR(b, loopBody.get(), BA.LeadingStmts, Ctx.ASTCtx);
  for (const auto &bc : BA.BaseCases) {
    if (bc.ValueStr.empty())
      IRBuilder::add(loopBody.get(),
                     IRBuilder::if_(IRExpr(bc.CondStr), IRBuilder::ret()));
    else
      IRBuilder::add(loopBody.get(),
                     IRBuilder::if_(IRExpr(bc.CondStr),
                                    IRBuilder::ret(IRExpr(bc.ValueStr))));
  }
  EmitStmtsToIR(b, loopBody.get(), BA.MiddleStmts, Ctx.ASTCtx);

  EmitTailRecParamUpdate(loopBody.get(), FD, dyn_cast<CallExpr>(BA.RecExpr),
                         Ctx.ASTCtx);

  IRBuilder::add(body.get(),
                 IRBuilder::while_(IRExpr("1"), std::move(loopBody)));
  b.function(sig, std::move(body));

  return PrintGeneratedUnit(b.unit);
}

} // namespace cps
