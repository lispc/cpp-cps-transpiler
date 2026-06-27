#include "transformation_rules.h"

namespace cps {

std::vector<std::unique_ptr<TransformationRule>> CreateDefaultRules() {
  std::vector<std::unique_ptr<TransformationRule>> rules;
  rules.emplace_back(std::make_unique<TailRecursionRule>());
  rules.emplace_back(std::make_unique<AccumulatorRule>());
  rules.emplace_back(std::make_unique<TuplingRule>());
  rules.emplace_back(std::make_unique<MemoizationRule>());
  rules.emplace_back(std::make_unique<BinaryStackRule>());
  rules.emplace_back(std::make_unique<TreeTraversalRule>());
  rules.emplace_back(std::make_unique<GenericStackRule>());
  rules.emplace_back(std::make_unique<DefunctionalizedRule>());
  return rules;
}

} // namespace cps
