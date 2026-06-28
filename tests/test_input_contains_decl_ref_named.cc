// Stub standard library so the transpiler can parse this file without system headers.
namespace std {
using size_t = unsigned long;

class string {
  const char *s_;

public:
  string() : s_("") {}
  string(const char *s) : s_(s ? s : "") {}
  string(const string &) = default;

  bool operator==(const string &o) const { return compare(o) == 0; }
  bool operator!=(const string &o) const { return compare(o) != 0; }

  int compare(const string &o) const {
    const char *a = s_;
    const char *b = o.s_;
    while (*a && *b && *a == *b) {
      ++a;
      ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
  }

  size_t size() const {
    size_t n = 0;
    while (s_[n])
      ++n;
    return n;
  }

  int compare(size_t pos, size_t len, const string &str) const {
    const char *a = s_ + pos;
    const char *b = str.s_;
    for (size_t i = 0; i < len && *a && *b; ++i, ++a, ++b) {
      if (*a != *b)
        return (unsigned char)*a - (unsigned char)*b;
    }
    return 0;
  }
};

template <typename T>
class vector {
  T Items[16];
  int Size;

public:
  vector() : Size(0) {}
  void push_back(const T &v) { Items[Size++] = v; }
  int size() const { return Size; }
  T *begin() { return Items; }
  T *end() { return Items + Size; }
  const T *begin() const { return Items; }
  const T *end() const { return Items + Size; }
};
} // namespace std

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

bool ContainsDeclRefNamed(const Expr *E, const std::string &Name) {
  if (!E)
    return false;
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (DRE->getDecl()->getNameAsString() == Name)
      return true;
  }
  for (const Stmt *Child : E->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)) {
      if (ContainsDeclRefNamed(ChildExpr, Name))
        return true;
    }
  }
  return false;
}
