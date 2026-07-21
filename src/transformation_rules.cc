#include "transformation_rules.h"

namespace cps {

namespace RuleCatalog {

const RuleInfo TailRecursion{"TailRecursionRule", 10};
const RuleInfo Accumulator{"AccumulatorRule", 20};
const RuleInfo Tupling{"TuplingRule", 30};
const RuleInfo Memoization{"MemoizationRule", 40};
const RuleInfo MultiDimMemo{"MultiDimMemoRule", 50};
const RuleInfo Unfold{"UnfoldRule", 25};
const RuleInfo BinaryStack{"BinaryStackRule", 100};
const RuleInfo TreeFold{"TreeFoldRule", 120};
const RuleInfo TreeTraversal{"TreeTraversalRule", 150};
const RuleInfo IsInTailPosition{"IsInTailPositionRule", 160};
const RuleInfo IsInTailPositionExpr{"IsInTailPositionExprRule", 160};
const RuleInfo IsPureExprIgnoringRecursiveCalls{
    "IsPureExprIgnoringRecursiveCallsRule", 160};
const RuleInfo IsReturnOrIfReturnOrSwitch{"IsReturnOrIfReturnOrSwitchRule", 160};
const RuleInfo UnwrapTrailingStmt{"UnwrapTrailingStmtRule", 160};
const RuleInfo FlattenIfElse{"FlattenIfElseRule", 160};
const RuleInfo EvalCondition{"EvalConditionRule", 160};
const RuleInfo ParseLinearTerms{"ParseLinearTermsRule", 160};
const RuleInfo StringStructuralRecursion{"StringStructuralRecursionRule", 150};
const RuleInfo GenericStack{"GenericStackRule", 200};
const RuleInfo Defunctionalized{"DefunctionalizedRule", 1000};

const std::vector<const RuleInfo *> &All() {
  static std::vector<const RuleInfo *> kAll = {
      &TailRecursion,
      &Accumulator,
      &Unfold,
      &Tupling,
      &Memoization,
      &MultiDimMemo,
      &BinaryStack,
      &TreeFold,
      &TreeTraversal,
      &IsInTailPosition,
      &IsInTailPositionExpr,
      &IsPureExprIgnoringRecursiveCalls,
      &IsReturnOrIfReturnOrSwitch,
      &UnwrapTrailingStmt,
      &FlattenIfElse,
      &EvalCondition,
      &ParseLinearTerms,
      &StringStructuralRecursion,
      &GenericStack,
      &Defunctionalized,
  };
  return kAll;
}

} // namespace RuleCatalog

std::vector<std::unique_ptr<TransformationRule>> CreateDefaultRules() {
  std::vector<std::unique_ptr<TransformationRule>> rules;
  for (const auto *info : RuleCatalog::All()) {
    if (info == &RuleCatalog::TailRecursion)
      rules.emplace_back(std::make_unique<TailRecursionRule>());
    else if (info == &RuleCatalog::Accumulator)
      rules.emplace_back(std::make_unique<AccumulatorRule>());
    else if (info == &RuleCatalog::Tupling)
      rules.emplace_back(std::make_unique<TuplingRule>());
    else if (info == &RuleCatalog::Memoization)
      rules.emplace_back(std::make_unique<MemoizationRule>());
    else if (info == &RuleCatalog::MultiDimMemo)
      rules.emplace_back(std::make_unique<MultiDimMemoRule>());
    else if (info == &RuleCatalog::Unfold)
      rules.emplace_back(std::make_unique<UnfoldRule>());
    else if (info == &RuleCatalog::BinaryStack)
      rules.emplace_back(std::make_unique<BinaryStackRule>());
    else if (info == &RuleCatalog::TreeFold)
      rules.emplace_back(std::make_unique<TreeFoldRule>());
    else if (info == &RuleCatalog::TreeTraversal)
      rules.emplace_back(std::make_unique<TreeTraversalRule>());
    else if (info == &RuleCatalog::IsInTailPosition)
      rules.emplace_back(std::make_unique<IsInTailPositionRule>());
    else if (info == &RuleCatalog::IsInTailPositionExpr)
      rules.emplace_back(std::make_unique<IsInTailPositionExprRule>());
    else if (info == &RuleCatalog::IsPureExprIgnoringRecursiveCalls)
      rules.emplace_back(std::make_unique<IsPureExprIgnoringRecursiveCallsRule>());
    else if (info == &RuleCatalog::IsReturnOrIfReturnOrSwitch)
      rules.emplace_back(std::make_unique<IsReturnOrIfReturnOrSwitchRule>());
    else if (info == &RuleCatalog::UnwrapTrailingStmt)
      rules.emplace_back(std::make_unique<UnwrapTrailingStmtRule>());
    else if (info == &RuleCatalog::FlattenIfElse)
      rules.emplace_back(std::make_unique<FlattenIfElseRule>());
    else if (info == &RuleCatalog::EvalCondition)
      rules.emplace_back(std::make_unique<EvalConditionRule>());
    else if (info == &RuleCatalog::ParseLinearTerms)
      rules.emplace_back(std::make_unique<ParseLinearTermsRule>());
    else if (info == &RuleCatalog::StringStructuralRecursion)
      rules.emplace_back(std::make_unique<StringStructuralRecursionRule>());
    else if (info == &RuleCatalog::GenericStack)
      rules.emplace_back(std::make_unique<GenericStackRule>());
    else if (info == &RuleCatalog::Defunctionalized)
      rules.emplace_back(std::make_unique<DefunctionalizedRule>());
  }
  return rules;
}

} // namespace cps
