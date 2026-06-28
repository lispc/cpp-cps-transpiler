// Direct copy of ExprContainsDeclRef from src/transformation_rules_helpers.cc.
// The surrounding type environment is minimal Clang-AST stubs so the transpiler
// can parse the file standalone.  The function body itself is unchanged.

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

bool ExprContainsDeclRef(const Expr *E, const ValueDecl *VD) {
  if (!E)
    return false;
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (DRE->getDecl() == VD)
      return true;
  }
  for (const Stmt *Child : E->children()) {
    if (ExprContainsDeclRef(dyn_cast_or_null<Expr>(Child), VD))
      return true;
  }
  return false;
}
