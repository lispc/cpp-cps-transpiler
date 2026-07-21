#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "output_ir.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace cps {

using namespace clang;

bool MemoizationRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                              const GenContext &Ctx) const {
  if (Ctx.RetType == "void" || !BA.RecExpr)
    return false;
  // Single-parameter functions only for now.
  if (Ctx.ParamNames.size() != 1)
    return false;

  // Require pure recursive expression (treating recursive calls as pure,
  // since they will be replaced by table lookups).
  if (!IsPureExprIgnoringRecursiveCalls(BA.RecExpr, Ctx.FuncName))
    return false;

  // Avoid pulling user-defined helper functions into the generated code;
  // leave expressions like min(f(n-1), f(n-2)) to GenericStackRule.
  if (ContainsNonRecursiveCall(BA.RecExpr, Ctx.FuncName))
    return false;

  std::vector<CallExpr *> holes;
  CollectHoles(BA.RecExpr, Ctx.FuncName, holes);
  // Need overlapping subproblems; a single recursive call is better handled
  // by AccumulatorRule or TailRecursionRule.
  if (holes.size() < 2)
    return false;

  const std::string &pName = Ctx.ParamNames[0];
  int maxOffset = 0;
  for (CallExpr *CE : holes) {
    if (CE->getNumArgs() != 1)
      return false;
    int offset = 0;
    if (!IsParamMinusConst(CE->getArg(0), pName, offset))
      return false;
    maxOffset = std::max(maxOffset, offset);
  }

  // Base cases must cover parameter values 0..maxOffset-1.
  for (int i = 0; i < maxOffset; ++i) {
    bool covered = false;
    for (const auto &bc : BA.BaseCases) {
      if (!bc.CondExpr)
        return false; // switch-derived base cases not supported here
      auto r = EvalConditionForParam(bc.CondExpr, pName, i);
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

CpsResult MemoizationRule::apply(const FunctionDecl *FD,
                                   const BodyAnalysis &BA,
                                   GenContext &Ctx) const {
  const std::string &pName = Ctx.ParamNames[0];

  std::vector<CallExpr *> holes;
  CollectHoles(BA.RecExpr, Ctx.FuncName, holes);

  int maxOffset = 0;
  std::unordered_map<const Expr *, std::string> repls;
  for (CallExpr *CE : holes) {
    int offset = 0;
    IsParamMinusConst(CE->getArg(0), pName, offset);
    maxOffset = std::max(maxOffset, offset);
    repls[CE] = "dp[i - " + std::to_string(offset) + "]";
  }
  std::string loopExpr =
      StripOuterParens(PrintExprWithReplacements(BA.RecExpr, repls, Ctx.ASTCtx));

  IRBuilder b;
  b.comment("=== Generated memoized code for function: " + Ctx.FuncName +
            " ===");
  b.include("vector");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  const ParmVarDecl *Param = FD->getParamDecl(0);

  auto body = IRBuilder::block();

  // Early base-case return for small inputs.
  {
    auto earlyBlk = IRBuilder::block();
    std::vector<std::pair<std::string, std::unique_ptr<IRBlock>>> branches;
    for (const auto &bc : BA.BaseCases) {
      std::string condStr =
          bc.CondExpr ? PrintExpr(bc.CondExpr, Ctx.ASTCtx) : bc.CondStr;
      std::string valStr =
          bc.ValueExpr ? PrintExpr(bc.ValueExpr, Ctx.ASTCtx) : bc.ValueStr;
      auto thenBlk = IRBuilder::block();
      IRBuilder::add(thenBlk.get(), IRBuilder::ret(IRExpr(valStr)));
      branches.emplace_back(std::move(condStr), std::move(thenBlk));
    }
    IRBuilder::add(earlyBlk.get(),
                   IRBuilder::ifChain(std::move(branches)));
    IRBuilder::add(earlyBlk.get(), IRBuilder::ret(IRExpr("0")));
    IRBuilder::add(body.get(),
                   IRBuilder::if_(IRExpr(pName + " <= " +
                                         std::to_string(maxOffset - 1)),
                                  std::move(earlyBlk)));
  }

  IRBuilder::add(body.get(),
                 IRBuilder::rawStmt("std::vector<" + Ctx.RetType + "> dp(" +
                                    pName + " + 1);"));

  for (int i = 0; i < maxOffset; ++i) {
    for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
      if (EvalConditionForParam(BA.BaseCases[bi].CondExpr, pName, i) ==
          EvalResult::True) {
        std::string baseExpr = ReplaceParamWithLiteral(
            BA.BaseCases[bi].ValueExpr, Param, std::to_string(i), Ctx.ASTCtx);
        IRBuilder::add(body.get(),
                       IRBuilder::expr(IRExpr("dp[" + std::to_string(i) +
                                              "] = " + baseExpr)));
        break;
      }
    }
  }

  auto forBody = IRBuilder::block();
  IRBuilder::add(forBody.get(),
                 IRBuilder::expr(IRExpr("dp[i] = " + loopExpr)));
  IRBuilder::add(body.get(),
                 IRBuilder::for_("int i = " + std::to_string(maxOffset),
                                 IRExpr("i <= " + pName), "++i",
                                 std::move(forBody)));
  IRBuilder::add(body.get(),
                 IRBuilder::ret(IRExpr("dp[" + pName + "]")));

  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
}

int MemoizationRule::cost() const { return RuleCatalog::Memoization.Cost; }

const char *MemoizationRule::name() const {
  return RuleCatalog::Memoization.Name;
}

} // namespace cps
