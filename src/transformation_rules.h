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

// Create the default ordered list of transformation rules.
// Rules are tried in order; the first one that applies wins.
std::vector<std::unique_ptr<TransformationRule>> CreateDefaultRules();

} // namespace cps

#endif // TRANSFORMATION_RULES_H
