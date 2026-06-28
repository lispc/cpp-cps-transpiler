#include <cstdio>
#include <string>
#include <vector>

struct Decl {
  std::string Name;
};

struct Stmt {
  virtual ~Stmt() = default;
};

struct Expr : Stmt {
  virtual const Expr *IgnoreParenImpCasts() const { return this; }
  virtual std::vector<const Stmt *> children() const { return {}; }
};

struct CallExpr : Expr {
  Decl Callee;
  std::vector<const Stmt *> Args;
  CallExpr(const char *name, std::vector<const Stmt *> args = {})
      : Callee{name}, Args{args} {}
  std::vector<const Stmt *> children() const override { return Args; }
};

std::string GetCalleeName(const CallExpr *CE) {
  return CE ? CE->Callee.Name : "";
}

template <typename To, typename From>
const To *dyn_cast(const From *p) {
  return dynamic_cast<const To *>(p);
}

template <typename To, typename From>
const To *dyn_cast_or_null(const From *p) {
  return p ? dynamic_cast<const To *>(p) : nullptr;
}
