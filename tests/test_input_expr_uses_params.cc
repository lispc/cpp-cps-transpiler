// Direct copy of ExprUsesParams from src/transformation_rules_helpers.cc.
// The surrounding environment is a minimal stub so the transpiler can parse the
// file standalone; the function body itself is unchanged.

namespace std {

class string {
  const char *Data;
public:
  string() : Data("") {}
  string(const char *S) : Data(S) {}
  string(const string &O) = default;
  bool operator==(const string &O) const {
    const char *A = Data;
    const char *B = O.Data;
    while (*A && *B && *A == *B) {
      ++A;
      ++B;
    }
    return *A == *B;
  }
};

template <typename Key>
class unordered_set {
public:
  unordered_set() = default;
  void insert(const Key &) {}
  unsigned long count(const Key &) const { return 0; }
};

} // namespace std

struct ValueDecl {
  std::string Name;
  ValueDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
};

struct FunctionDecl : ValueDecl {
  FunctionDecl(const std::string &N) : ValueDecl(N) {}
};

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

struct Expr : Stmt {
  virtual StmtRange children() const { return StmtRange(); }
};

struct DeclRefExpr : Expr {
  ValueDecl *Decl;
  DeclRefExpr(ValueDecl *D) : Decl(D) {}
  const ValueDecl *getDecl() const { return Decl; }
};

struct CallExpr : Expr {
  FunctionDecl *Callee;
  StmtRange Args;

  CallExpr(FunctionDecl *C) : Callee(C) {}
  void addArg(Expr *A) { Args.push_back(A); }

  const FunctionDecl *getDirectCallee() const { return Callee; }

  StmtRange children() const override {
    StmtRange R;
    for (Stmt *S : Args)
      R.push_back(S);
    return R;
  }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) {
  return dynamic_cast<const T *>(P);
}

template <typename T, typename U>
const T *dyn_cast_or_null(const U *P) {
  return P ? dynamic_cast<const T *>(P) : nullptr;
}

bool ExprUsesParams(const Expr *E,
                    const std::unordered_set<std::string> &ParamNames) {
  if (!E)
    return false;
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const ValueDecl *VD = DRE->getDecl()) {
      if (ParamNames.count(VD->getNameAsString()))
        return true;
    }
  }
  for (const Stmt *Child : E->children()) {
    if (ExprUsesParams(dyn_cast_or_null<Expr>(Child), ParamNames))
      return true;
  }
  return false;
}
