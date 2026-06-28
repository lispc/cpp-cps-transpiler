#include <cstdio>
#include <string>
#include <vector>

struct Decl {
  std::string Name;
  const std::string &getNameAsString() const { return Name; }
};

struct Stmt {
  virtual ~Stmt() = default;
};

struct Expr : Stmt {
  virtual const Expr *IgnoreParenImpCasts() const { return this; }
  virtual std::vector<const Stmt *> children() const { return {}; }
};

struct DeclRefExpr : Expr {
  Decl Ref;
  DeclRefExpr(const char *name) : Ref{name} {}
  const Decl *getDecl() const { return &Ref; }
  std::vector<const Stmt *> children() const override { return {}; }
};

struct CallExpr : Expr {
  std::vector<const Stmt *> Args;
  CallExpr(std::vector<const Stmt *> args = {}) : Args{args} {}
  std::vector<const Stmt *> children() const override { return Args; }
};

template <typename To, typename From>
const To *dyn_cast(const From *p) {
  return dynamic_cast<const To *>(p);
}

template <typename To, typename From>
const To *dyn_cast_or_null(const From *p) {
  return p ? dynamic_cast<const To *>(p) : nullptr;
}
