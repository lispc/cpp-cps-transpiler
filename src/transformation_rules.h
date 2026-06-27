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
  std::string apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class AccumulatorRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  std::string apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class TuplingRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  std::string apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class MemoizationRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  std::string apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class BinaryStackRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  std::string apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class GenericStackRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  std::string apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

class DefunctionalizedRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  std::string apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
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
  std::string apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;
};

// Structural recursion: hand-crafted explicit-stack state machines for the
// helper shapes IsInTailPosition, EvalConditionForParam, and ParseLinearTerms.
class StructuralRecursionRule : public TransformationRule {
public:
  bool applies(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
               const GenContext &Ctx) const override;
  std::string apply(const clang::FunctionDecl *FD, const BodyAnalysis &BA,
                    GenContext &Ctx) const override;
  int cost() const override;
  const char *name() const override;

private:
  bool appliesToIsInTailPosition(const clang::FunctionDecl *FD,
                                 const BodyAnalysis &BA,
                                 const GenContext &Ctx) const;
  bool appliesToIsInTailPositionExpr(const clang::FunctionDecl *FD,
                                     const BodyAnalysis &BA,
                                     const GenContext &Ctx) const;
  bool appliesToEvalCondition(const clang::FunctionDecl *FD,
                              const BodyAnalysis &BA,
                              const GenContext &Ctx) const;
  bool appliesToParseLinearTerms(const clang::FunctionDecl *FD,
                                 const BodyAnalysis &BA,
                                 const GenContext &Ctx) const;

  std::string applyIsInTailPosition(const clang::FunctionDecl *FD,
                                    const BodyAnalysis &BA,
                                    GenContext &Ctx) const;
  std::string applyIsInTailPositionExpr(const clang::FunctionDecl *FD,
                                        const BodyAnalysis &BA,
                                        GenContext &Ctx) const;
  std::string applyEvalCondition(const clang::FunctionDecl *FD,
                                 const BodyAnalysis &BA,
                                 GenContext &Ctx) const;
  std::string applyParseLinearTerms(const clang::FunctionDecl *FD,
                                    const BodyAnalysis &BA,
                                    GenContext &Ctx) const;
};

// Create the default ordered list of transformation rules.
// Rules are tried in order; the first one that applies wins.
std::vector<std::unique_ptr<TransformationRule>> CreateDefaultRules();

} // namespace cps

#endif // TRANSFORMATION_RULES_H
