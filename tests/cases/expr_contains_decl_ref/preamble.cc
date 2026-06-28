#include <string>
#include <vector>

struct Stmt {
  virtual ~Stmt() = default;
};

struct StmtRange {
  static constexpr int Max = 16;
  Stmt *Items[Max];
  int Size;

  StmtRange() : Size(0) {}
  void push_back(Stmt *S) { Items[Size++] = S; }

  Stmt **begin() { return Items; }
  Stmt **end() { return Items + Size; }
  Stmt *const *begin() const { return Items; }
  Stmt *const *end() const { return Items + Size; }

  struct RevIt {
    Stmt **P;
    Stmt *&operator*() { return *(P - 1); }
    RevIt &operator++() { --P; return *this; }
    bool operator!=(const RevIt &O) const { return P != O.P; }
  };
  struct ConstRevIt {
    Stmt *const *P;
    Stmt *const &operator*() { return *(P - 1); }
    ConstRevIt &operator++() { --P; return *this; }
    bool operator!=(const ConstRevIt &O) const { return P != O.P; }
  };
  RevIt rbegin() { return RevIt{Items + Size}; }
  RevIt rend() { return RevIt{Items}; }
  ConstRevIt rbegin() const { return ConstRevIt{Items + Size}; }
  ConstRevIt rend() const { return ConstRevIt{Items}; }
};

struct ValueDecl {
  virtual ~ValueDecl() = default;
};

struct Expr : Stmt {
  virtual StmtRange children() const { return StmtRange(); }
};

struct DeclRefExpr : Expr {
  const ValueDecl *VD;
  DeclRefExpr(const ValueDecl *V) : VD(V) {}
  const ValueDecl *getDecl() const { return VD; }
};

struct CallExpr : Expr {
  StmtRange Args;
  void addArg(Expr *A) { Args.push_back(A); }
  StmtRange children() const override { return Args; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) {
  return dynamic_cast<const T *>(P);
}

template <typename T, typename U>
const T *dyn_cast_or_null(const U *P) {
  return P ? dynamic_cast<const T *>(P) : nullptr;
}
