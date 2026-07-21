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

bool TuplingRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                          const GenContext &Ctx) const {
  if (Ctx.RetType == "void" || !BA.RecExpr)
    return false;
  if (Ctx.ParamNames.empty())
    return false;

  // Switch-derived base cases have synthetic string conditions; tupling
  // needs AST expressions to evaluate them for n=0..k-1.
  for (const auto &bc : BA.BaseCases) {
    if (!bc.CondExpr)
      return false;
    if (!bc.ValueExpr || !IsPureExpr(bc.ValueExpr))
      return false;
  }

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
      auto r = EvalConditionForParam(bc.CondExpr, Ctx.ParamNames[0], j);
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

CpsResult TuplingRule::apply(const FunctionDecl *FD, const BodyAnalysis &BA,
                               GenContext &Ctx) const {
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
      StripOuterParens(PrintExprWithReplacements(BA.RecExpr, repls, Ctx.ASTCtx));

  IRBuilder b;
  b.comment("=== Generated tupling code for function: " + Ctx.FuncName +
            " ===");
  b.include("array");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  const ParmVarDecl *Param = FD->getParamDecl(0);

  auto body = IRBuilder::block();

  // Early base-case return.
  {
    auto earlyBlk = IRBuilder::block();
    std::vector<std::pair<std::string, std::unique_ptr<IRBlock>>> branches;
    for (const auto &bc : BA.BaseCases) {
      std::string condStr = PrintBaseCaseCond(bc, Ctx.ASTCtx);
      std::string valStr = PrintBaseCaseValue(bc, Ctx.ASTCtx);
      auto thenBlk = IRBuilder::block();
      IRBuilder::add(thenBlk.get(), IRBuilder::ret(IRExpr(valStr)));
      branches.emplace_back(std::move(condStr), std::move(thenBlk));
    }
    IRBuilder::add(earlyBlk.get(),
                   IRBuilder::ifChain(std::move(branches)));
    IRBuilder::add(earlyBlk.get(), IRBuilder::ret(IRExpr("0")));
    IRBuilder::add(body.get(),
                   IRBuilder::if_(IRExpr(pName + " <= " +
                                         std::to_string(k - 1)),
                                  std::move(earlyBlk)));
  }

  IRBuilder::add(body.get(),
                 IRBuilder::var("std::array<" + Ctx.RetType + ", " +
                                    std::to_string(k) + ">",
                                "vals"));

  for (int j = 0; j < k; ++j) {
    for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
      if (EvalConditionForParam(BA.BaseCases[bi].CondExpr, pName, j) ==
          EvalResult::True) {
        std::string baseExpr = ReplaceParamWithLiteral(
            BA.BaseCases[bi].ValueExpr, Param, std::to_string(j), Ctx.ASTCtx);
        IRBuilder::add(body.get(),
                       IRBuilder::expr(IRExpr("vals[" + std::to_string(j) +
                                              "] = " + baseExpr)));
        break;
      }
    }
  }

  auto forBody = IRBuilder::block();
  IRBuilder::add(forBody.get(),
                 IRBuilder::var(Ctx.RetType, "next", IRExpr(nextExpr)));
  for (int j = 0; j < k - 1; ++j)
    IRBuilder::add(forBody.get(),
                   IRBuilder::expr(IRExpr("vals[" + std::to_string(j) +
                                          "] = vals[" + std::to_string(j + 1) +
                                          "]")));
  IRBuilder::add(forBody.get(),
                 IRBuilder::expr(IRExpr("vals[" + std::to_string(k - 1) +
                                        "] = next")));
  IRBuilder::add(body.get(),
                 IRBuilder::for_(pType + " i = " + std::to_string(k),
                                 IRExpr("i <= " + pName), "++i",
                                 std::move(forBody)));
  IRBuilder::add(body.get(),
                 IRBuilder::ret(IRExpr("vals[" + std::to_string(k - 1) +
                                       "]")));

  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
}

} // namespace cps
