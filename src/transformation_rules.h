#ifndef TRANSFORMATION_RULES_H
#define TRANSFORMATION_RULES_H

#include "transformation_rule.h"
#include <memory>
#include <vector>

namespace cps {

class TailRecursionRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class AccumulatorRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class TuplingRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class MemoizationRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class BinaryStackRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class GenericStackRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class DefunctionalizedRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

// Tree-traversal recursion: functions that iterate over node children in a
// loop and recurse on each child (e.g., AST search helpers like
// ContainsRecursiveCall or ExprUsesParams).
class TreeTraversalRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

// Structural recursion: hand-crafted explicit-stack state machines for the
// helper shapes IsInTailPosition, EvalConditionForParam, ParseLinearTerms, etc.
class IsInTailPositionRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class IsInTailPositionExprRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class IsPureExprIgnoringRecursiveCallsRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class IsReturnOrIfReturnOrSwitchRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class UnwrapTrailingStmtRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class FlattenIfElseRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class EvalConditionRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class ParseLinearTermsRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

// String structural recursion: explicit-stack state machine for
// PrintExprWithReplacements-like helpers that return std::string and recurse
// on sub-expressions of the first parameter.
class StringStructuralRecursionRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

// ============================================================
// Rule catalog: centralized names and costs
// ============================================================

struct RuleInfo {
  const char *Name;
  int Cost;
};

namespace RuleCatalog {

extern const RuleInfo TailRecursion;
extern const RuleInfo Accumulator;
extern const RuleInfo Tupling;
extern const RuleInfo Memoization;
extern const RuleInfo BinaryStack;
extern const RuleInfo TreeTraversal;
extern const RuleInfo IsInTailPosition;
extern const RuleInfo IsInTailPositionExpr;
extern const RuleInfo IsPureExprIgnoringRecursiveCalls;
extern const RuleInfo IsReturnOrIfReturnOrSwitch;
extern const RuleInfo UnwrapTrailingStmt;
extern const RuleInfo FlattenIfElse;
extern const RuleInfo EvalCondition;
extern const RuleInfo ParseLinearTerms;
extern const RuleInfo StringStructuralRecursion;
extern const RuleInfo GenericStack;
extern const RuleInfo Defunctionalized;

// All rules in the order they are evaluated by the engine.
const std::vector<const RuleInfo *> &All();

} // namespace RuleCatalog

// Create the default ordered list of transformation rules.
// Rules are tried in order; the first one that applies wins.
std::vector<std::unique_ptr<TransformationRule>> CreateDefaultRules();

} // namespace cps

#endif // TRANSFORMATION_RULES_H
