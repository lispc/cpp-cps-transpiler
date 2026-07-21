#include "transformation_rules.h"

namespace cps {

namespace {

template <typename R>
std::unique_ptr<TransformationRule> MakeRule() {
  return std::make_unique<R>();
}

} // anonymous namespace

namespace RuleCatalog {

const RuleInfo TailRecursion{"TailRecursionRule", 10, &MakeRule<TailRecursionRule>};
const RuleInfo Accumulator{"AccumulatorRule", 20, &MakeRule<AccumulatorRule>};
const RuleInfo Tupling{"TuplingRule", 30, &MakeRule<TuplingRule>};
const RuleInfo Memoization{"MemoizationRule", 40, &MakeRule<MemoizationRule>};
const RuleInfo MultiDimMemo{"MultiDimMemoRule", 50, &MakeRule<MultiDimMemoRule>};
const RuleInfo Unfold{"UnfoldRule", 25, &MakeRule<UnfoldRule>};
const RuleInfo BinaryStack{"BinaryStackRule", 100, &MakeRule<BinaryStackRule>};
const RuleInfo TreeFold{"TreeFoldRule", 120, &MakeRule<TreeFoldRule>};
const RuleInfo TreeTraversal{"TreeTraversalRule", 150, &MakeRule<TreeTraversalRule>};
const RuleInfo IsInTailPosition{"IsInTailPositionRule", 160, &MakeRule<IsInTailPositionRule>};
const RuleInfo IsInTailPositionExpr{"IsInTailPositionExprRule", 160, &MakeRule<IsInTailPositionExprRule>};
const RuleInfo IsPureExprIgnoringRecursiveCalls{
    "IsPureExprIgnoringRecursiveCallsRule", 160,
    &MakeRule<IsPureExprIgnoringRecursiveCallsRule>};
const RuleInfo IsReturnOrIfReturnOrSwitch{"IsReturnOrIfReturnOrSwitchRule", 160, &MakeRule<IsReturnOrIfReturnOrSwitchRule>};
const RuleInfo UnwrapTrailingStmt{"UnwrapTrailingStmtRule", 160, &MakeRule<UnwrapTrailingStmtRule>};
const RuleInfo FlattenIfElse{"FlattenIfElseRule", 160, &MakeRule<FlattenIfElseRule>};
const RuleInfo EvalCondition{"EvalConditionRule", 160, &MakeRule<EvalConditionRule>};
const RuleInfo ParseLinearTerms{"ParseLinearTermsRule", 160, &MakeRule<ParseLinearTermsRule>};
const RuleInfo StringStructuralRecursion{"StringStructuralRecursionRule", 150, &MakeRule<StringStructuralRecursionRule>};
const RuleInfo GenericStack{"GenericStackRule", 200, &MakeRule<GenericStackRule>};
const RuleInfo Defunctionalized{"DefunctionalizedRule", 1000, &MakeRule<DefunctionalizedRule>};

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

std::vector<RuleEntry> CreateDefaultRules() {
  std::vector<RuleEntry> rules;
  for (const RuleInfo *info : RuleCatalog::All())
    rules.push_back({info, info->Create()});
  return rules;
}

} // namespace cps
