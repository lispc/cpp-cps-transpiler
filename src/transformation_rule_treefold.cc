#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "output_ir.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace cps {

using namespace clang;

namespace {

// True when E is a `->member` (possibly chained) access rooted at the
// function's node parameter, e.g. `t->left` or `t->left->right`.
bool IsMemberChainOnParam(const Expr *E, const ParmVarDecl *P) {
  E = E->IgnoreParenImpCasts();
  if (!isa<MemberExpr>(E))
    return false;
  const Expr *Base = E;
  while (const auto *ME = dyn_cast<MemberExpr>(Base))
    Base = ME->getBase()->IgnoreParenImpCasts();
  const auto *DRE = dyn_cast<DeclRefExpr>(Base);
  return DRE && DRE->getDecl()->getCanonicalDecl() == P->getCanonicalDecl();
}

} // anonymous namespace

bool TreeFoldRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                           const GenContext &Ctx) const {
  if (Ctx.RetType == "void" || !BA.RecExpr)
    return false;
  if (Ctx.Params.size() != 1 ||
      !Ctx.Params[0]->getType()->isPointerType())
    return false;
  if (!BA.MiddleStmts.empty() || BA.BaseCases.empty())
    return false;
  if (!IsPureExprIgnoringRecursiveCalls(BA.RecExpr, Ctx.FuncName))
    return false;

  std::vector<CallExpr *> holes;
  CollectHoles(BA.RecExpr, Ctx.FuncName, holes);
  if (holes.empty())
    return false;
  for (const CallExpr *hole : holes) {
    if (hole->getNumArgs() != 1)
      return false;
    if (!IsMemberChainOnParam(hole->getArg(0), Ctx.Params[0]))
      return false;
  }
  return true;
}

CpsResult TreeFoldRule::apply(const FunctionDecl *FD, const BodyAnalysis &BA,
                              GenContext &Ctx) const {
  std::vector<CallExpr *> holes;
  CollectHoles(BA.RecExpr, Ctx.FuncName, holes);
  const size_t K = holes.size();

  const ParmVarDecl *NodeParam = Ctx.Params[0];
  std::string nodeType = GetParamStorageType(NodeParam);
  std::string frameName = "__cps_" + Ctx.FuncName + "Frame";
  std::string stackName = "__cps_stack";
  std::string valuesName = "__cps_values";
  std::string curName = "__cps_f";
  std::string nodeExpr = curName + ".node";

  IRBuilder b;
  b.comment("=== Generated tree-fold code for function: " + Ctx.FuncName +
            " ===");
  b.include("vector");
  if (ContainsCallToOneOf(BA.RecExpr, {"min", "max"}))
    b.include("algorithm");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  auto body = IRBuilder::block();

  // Frame: the node plus a flag telling whether its children are done.
  {
    IRStructData frame;
    frame.name = frameName;
    frame.fields.emplace_back(nodeType, "node");
    frame.fields.emplace_back("bool", "expanded");
    IRBuilder::add(body.get(), IRBuilder::localStruct(std::move(frame)));
  }

  IRBuilder::add(body.get(),
                 IRBuilder::var("std::vector<" + frameName + ">", stackName));
  IRBuilder::add(body.get(),
                 IRBuilder::expr(IRExpr(stackName + ".push_back({" +
                                        NodeParam->getNameAsString() +
                                        ", false})")));
  IRBuilder::add(body.get(),
                 IRBuilder::var("std::vector<" + Ctx.RetType + ">",
                                valuesName));

  auto loopBody = IRBuilder::block();
  IRBuilder::add(loopBody.get(),
                 IRBuilder::var("auto", curName,
                                IRExpr(stackName + ".back()")));
  IRBuilder::add(loopBody.get(),
                 IRBuilder::expr(IRExpr(stackName + ".pop_back()")));

  // Map the node parameter to the current frame's node for condition/value
  // printing.
  std::unordered_map<const ValueDecl *, std::string> declRepls;
  declRepls[NodeParam] = nodeExpr;

  BaseCaseRename nodeRename;
  nodeRename.DeclRepls = declRepls;
  nodeRename.StringRepls.emplace_back(NodeParam->getNameAsString(), nodeExpr);
  nodeRename.StripParens = true;
  nodeRename.UseDeclPrinter = true;

  auto printOnNode = [&](const BaseCase &bc, bool cond) -> std::string {
    return cond ? PrintBaseCaseCond(bc, Ctx.ASTCtx, nodeRename)
                : PrintBaseCaseValue(bc, Ctx.ASTCtx, nodeRename);
  };

  // Expand branch: push the continuation frame, then the child frames in
  // reverse hole order so that the leftmost hole is evaluated first.
  auto expandBlk = IRBuilder::block();
  IRBuilder::add(expandBlk.get(),
                 IRBuilder::expr(IRExpr(stackName + ".push_back({" +
                                        nodeExpr + ", true})")));
  for (size_t k = K; k-- > 0;) {
    std::string child = StripOuterParens(PrintExprWithDeclReplacements(
        holes[k]->getArg(0), declRepls, Ctx.ASTCtx));
    IRBuilder::add(expandBlk.get(),
                   IRBuilder::expr(IRExpr(stackName + ".push_back({" +
                                          child + ", false})")));
  }

  // Base-case chain: push the base value; otherwise expand.
  auto notExpandedBlk = IRBuilder::block();
  {
    std::vector<std::pair<std::string, std::unique_ptr<IRBlock>>> branches;
    for (const auto &bc : BA.BaseCases) {
      auto thenBlk = IRBuilder::block();
      IRBuilder::add(thenBlk.get(),
                     IRBuilder::expr(IRExpr(valuesName + ".push_back(" +
                                            printOnNode(bc, false) + ")")));
      branches.emplace_back(printOnNode(bc, true), std::move(thenBlk));
    }
    IRBuilder::add(notExpandedBlk.get(),
                   IRBuilder::ifChain(std::move(branches),
                                      std::move(expandBlk)));
  }

  // Combine branch: pop the child values (reverse order) and push the
  // combined result.
  auto combineBlk = IRBuilder::block();
  {
    std::unordered_map<const Expr *, std::string> holeRepls;
    for (size_t k = 0; k < K; ++k)
      holeRepls[holes[k]] = "__cps_v" + std::to_string(k);
    for (size_t k = K; k-- > 0;) {
      IRBuilder::add(combineBlk.get(),
                     IRBuilder::var("auto", "__cps_v" + std::to_string(k),
                                    IRExpr(valuesName + ".back()")));
      IRBuilder::add(combineBlk.get(),
                     IRBuilder::expr(IRExpr(valuesName + ".pop_back()")));
    }
    std::string combined = StripOuterParens(PrintExprWithReplacements(
        BA.RecExpr, holeRepls, declRepls, Ctx.ASTCtx));
    IRBuilder::add(combineBlk.get(),
                   IRBuilder::expr(IRExpr(valuesName + ".push_back(" +
                                          combined + ")")));
  }

  IRBuilder::add(loopBody.get(),
                 IRBuilder::if_(IRExpr("!" + curName + ".expanded"),
                                std::move(notExpandedBlk),
                                std::move(combineBlk)));
  IRBuilder::add(body.get(),
                 IRBuilder::while_(IRExpr("!" + stackName + ".empty()"),
                                   std::move(loopBody)));
  IRBuilder::add(body.get(),
                 IRBuilder::ret(IRExpr(valuesName + ".back()")));

  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
}

} // namespace cps
