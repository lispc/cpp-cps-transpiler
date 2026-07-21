#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "output_ir.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include <string>
#include <vector>

namespace cps {

using namespace clang;

namespace {

// Shape extracted from the unfold pattern:
//   [leading] (base cases) auto r = f(next_args); <post stmts>; return r;
struct UnfoldShape {
  const VarDecl *ResultVar = nullptr;   // the local `r`
  const CallExpr *Hole = nullptr;       // the single recursive call
  size_t FirstPostStmt = 0;             // index into BA.MiddleStmts
  // decreasingParams[d]: parameter d strictly decreases via `p - const`.
  std::vector<bool> decreasing;
};

// Validate the unfold shape and fill Out. See the rule class comment for the
// supported pattern.
bool AnalyzeUnfoldShape(const BodyAnalysis &BA, const GenContext &Ctx,
                        UnfoldShape &Out) {
  if (!BA.RecExpr || BA.MiddleStmts.empty() || BA.BaseCases.empty())
    return false;

  // The final return must be a plain reference to a local variable.
  // Class-type returns wrap the reference in ExprWithCleanups and a copy
  // constructor call, so peel both.
  const Expr *RetE = BA.RecExpr->IgnoreImplicit();
  while (const auto *Ctor = dyn_cast<CXXConstructExpr>(RetE)) {
    if (Ctor->getNumArgs() != 1)
      break;
    RetE = Ctor->getArg(0)->IgnoreImplicit();
  }
  const auto *RetRef = dyn_cast<DeclRefExpr>(RetE);
  if (!RetRef)
    return false;
  const auto *ResultVar = dyn_cast<VarDecl>(RetRef->getDecl());
  if (!ResultVar)
    return false;

  // The first middle statement must declare that variable, initialised by
  // exactly the recursive call.
  const auto *DS = dyn_cast<DeclStmt>(BA.MiddleStmts[0]);
  if (!DS || !DS->isSingleDecl() || DS->getSingleDecl() != ResultVar)
    return false;
  const Expr *Init = ResultVar->getInit();
  if (!Init)
    return false;
  const auto *Hole = dyn_cast<CallExpr>(Init->IgnoreImplicit());
  if (!Hole)
    return false;
  const FunctionDecl *Callee = Hole->getDirectCallee();
  if (!Callee || Callee->getNameAsString() != Ctx.FuncName)
    return false;
  if (Hole->getNumArgs() != Ctx.Params.size())
    return false;

  // No other middle statement may contain a recursive call.
  for (size_t i = 1; i < BA.MiddleStmts.size(); ++i) {
    std::vector<CallExpr *> calls;
    CollectRecursiveCallsInStmt(BA.MiddleStmts[i], Ctx.FuncName, calls);
    if (!calls.empty())
      return false;
  }

  // Hole arguments: every position is either the unchanged parameter
  // (pass-through) or `p - <positive const>` (decreasing); at least one
  // position must decrease. All arguments must be pure.
  Out.decreasing.assign(Ctx.Params.size(), false);
  bool anyDecreasing = false;
  for (unsigned a = 0; a < Hole->getNumArgs(); ++a) {
    const Expr *Arg = Hole->getArg(a);
    if (!IsPureExpr(Arg))
      return false;
    const ParmVarDecl *P = Ctx.Params[a];
    const Expr *Clean = Arg->IgnoreParenImpCasts();
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Clean)) {
      if (DRE->getDecl()->getCanonicalDecl() == P->getCanonicalDecl())
        continue; // pass-through
    }
    int off = 0;
    if (!IsParamMinusConst(Arg, P->getNameAsString(), off))
      return false;
    if (!P->getType()->isIntegralOrEnumerationType())
      return false;
    Out.decreasing[a] = true;
    anyDecreasing = true;
  }
  if (!anyDecreasing)
    return false;

  Out.ResultVar = ResultVar;
  Out.Hole = Hole;
  Out.FirstPostStmt = 1;
  return true;
}

} // anonymous namespace

bool UnfoldRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                         const GenContext &Ctx) const {
  if (Ctx.RetType == "void")
    return false;
  UnfoldShape shape;
  return AnalyzeUnfoldShape(BA, Ctx, shape);
}

CpsResult UnfoldRule::apply(const FunctionDecl *FD, const BodyAnalysis &BA,
                            GenContext &Ctx) const {
  UnfoldShape shape;
  AnalyzeUnfoldShape(BA, Ctx, shape);

  std::string rName = shape.ResultVar->getNameAsString();

  IRBuilder b;
  b.comment("=== Generated unfold code for function: " + Ctx.FuncName +
            " ===");
  b.include("vector");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  auto body = IRBuilder::block();

  EmitStmtsToIR(b, body.get(), BA.LeadingStmts, Ctx.ASTCtx);

  // Phase 1: walk the parameters down to the base case, recording the values
  // of every decreasing parameter.
  std::vector<std::string> pathVar(Ctx.Params.size());
  {
    auto walkBody = IRBuilder::block();
    for (size_t d = 0; d < Ctx.Params.size(); ++d) {
      if (!shape.decreasing[d])
        continue;
      const ParmVarDecl *P = Ctx.Params[d];
      pathVar[d] = "__cps_path_" + Ctx.ParamNames[d];
      IRBuilder::add(body.get(),
                     IRBuilder::var("std::vector<" + GetParamStorageType(P) +
                                        ">",
                                    pathVar[d]));
      IRBuilder::add(walkBody.get(),
                     IRBuilder::expr(IRExpr(pathVar[d] + ".push_back(" +
                                            Ctx.ParamNames[d] + ")")));
    }
    EmitTailRecParamUpdate(walkBody.get(), FD, shape.Hole, Ctx.ASTCtx);

    std::string walkCond;
    for (size_t i = 0; i < BA.BaseCases.size(); ++i) {
      if (i > 0)
        walkCond += " && ";
      walkCond += "!(" + BA.BaseCases[i].CondStr + ")";
    }
    IRBuilder::add(body.get(),
                   IRBuilder::while_(IRExpr(walkCond), std::move(walkBody)));
  }

  // Seed: evaluate the base-case chain with the boundary parameters.
  {
    std::string rType =
        NormalizeTypeName(shape.ResultVar->getType().getAsString());
    IRBuilder::add(body.get(),
                   IRBuilder::rawStmt(rType + " " + rName + " = " +
                                      GetDefaultValueForType(
                                          shape.ResultVar->getType()) +
                                      ";"));
    std::vector<std::pair<std::string, std::unique_ptr<IRBlock>>> branches;
    for (const auto &bc : BA.BaseCases) {
      auto thenBlk = IRBuilder::block();
      IRBuilder::add(thenBlk.get(),
                     IRBuilder::expr(IRExpr(rName + " = " + bc.ValueStr)));
      branches.emplace_back(bc.CondStr, std::move(thenBlk));
    }
    IRBuilder::add(body.get(), IRBuilder::ifChain(std::move(branches)));
  }

  // Phase 2: replay the post-processing statements on the way back up.
  {
    auto upBody = IRBuilder::block();
    for (size_t d = 0; d < Ctx.Params.size(); ++d) {
      if (!shape.decreasing[d])
        continue;
      IRBuilder::add(upBody.get(),
                     IRBuilder::var("auto", Ctx.ParamNames[d],
                                    IRExpr(pathVar[d] + ".back()")));
      IRBuilder::add(upBody.get(),
                     IRBuilder::expr(IRExpr(pathVar[d] + ".pop_back()")));
    }
    std::vector<const Stmt *> postStmts(BA.MiddleStmts.begin() +
                                            shape.FirstPostStmt,
                                        BA.MiddleStmts.end());
    EmitStmtsToIR(b, upBody.get(), postStmts, Ctx.ASTCtx);

    std::string upCond;
    for (size_t d = 0; d < Ctx.Params.size(); ++d) {
      if (!shape.decreasing[d])
        continue;
      if (!upCond.empty())
        upCond += " && ";
      upCond += "!" + pathVar[d] + ".empty()";
    }
    IRBuilder::add(body.get(),
                   IRBuilder::while_(IRExpr(upCond), std::move(upBody)));
  }

  IRBuilder::add(body.get(), IRBuilder::ret(IRExpr(rName)));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
}

} // namespace cps
