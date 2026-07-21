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
};

class AccumulatorRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

class TuplingRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

class MemoizationRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

// Multi-dimensional memoization: recurrences over two or more integer index
// parameters with constant-offset recursive calls (grid paths, LCS, edit
// distance). Non-index parameters may be passed through unchanged.
class MultiDimMemoRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

// Unfold (construction-side) recursion: the recursive call sits in a middle
// statement's initializer and the result is built up layer by layer, e.g.
//   auto r = f(n - 1); r.push_back(n); return r;
// Converted to a two-phase loop: walk parameters down to the seed, then
// replay the post-processing statements on the way back up.
class UnfoldRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

class BinaryStackRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

class GenericStackRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

class DefunctionalizedRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
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
};

// Tree fold (catamorphism/paramorphism): value-returning recursion over a
// node pointer where every recursive call targets a ->member chain of the
// node (e.g. t->left / t->right). The combine expression may also reference
// the child nodes themselves (paramorphism). Generates a post-order
// two-stack traversal without marker machinery.
class TreeFoldRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

// Structural recursion: hand-crafted explicit-stack state machines for the
// helper shapes IsInTailPosition, EvalConditionForParam, ParseLinearTerms, etc.
class IsInTailPositionRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

class IsInTailPositionExprRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

class IsPureExprIgnoringRecursiveCallsRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

class IsReturnOrIfReturnOrSwitchRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

class UnwrapTrailingStmtRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

class FlattenIfElseRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

class EvalConditionRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
};

class ParseLinearTermsRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  CpsResult apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
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
};

// ============================================================
// Rule catalog: centralized names, costs, and factories
// ============================================================

struct RuleInfo {
  const char *Name;
  // Estimated runtime cost of the generated code; lower is better.  The
  // engine picks the applicable rule with the smallest cost; ties are broken
  // by the rule's position in RuleCatalog::All().
  int Cost;
  std::unique_ptr<TransformationRule> (*Create)();
};

namespace RuleCatalog {

extern const RuleInfo TailRecursion;
extern const RuleInfo Accumulator;
extern const RuleInfo Tupling;
extern const RuleInfo Memoization;
extern const RuleInfo MultiDimMemo;
extern const RuleInfo Unfold;
extern const RuleInfo BinaryStack;
extern const RuleInfo TreeTraversal;
extern const RuleInfo TreeFold;
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

// A rule instance paired with its catalog metadata.
struct RuleEntry {
  const RuleInfo *Info;
  std::unique_ptr<TransformationRule> Rule;
};

// Create the default ordered list of transformation rules.
// Applicable rules compete on RuleInfo::Cost; the lowest cost wins.
std::vector<RuleEntry> CreateDefaultRules();

} // namespace cps

#endif // TRANSFORMATION_RULES_H
