// Direct copy of FindRecursiveCallReturnIf from src/transformation_rules_helpers.cc.
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

} // namespace std

struct FunctionDecl {
  std::string Name;
  FunctionDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
};

struct Stmt;

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

struct Stmt {
  virtual ~Stmt() = default;
  virtual StmtRange children() const { return StmtRange(); }
};

struct Expr : Stmt {
  virtual const Expr *IgnoreParenImpCasts() const { return this; }
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

struct IfStmt : Stmt {
  const Expr *Cond;
  const Stmt *Then;
  IfStmt(const Expr *C, const Stmt *T) : Cond(C), Then(T) {}
  const Expr *getCond() const { return Cond; }
  const Stmt *getThen() const { return Then; }
  StmtRange children() const override {
    StmtRange R;
    R.push_back(const_cast<Stmt *>(Then));
    return R;
  }
};

struct ReturnStmt : Stmt {};

template <typename T, typename U>
const T *dyn_cast(const U *P) {
  return dynamic_cast<const T *>(P);
}

template <typename T, typename U>
const T *dyn_cast_or_null(const U *P) {
  return P ? dynamic_cast<const T *>(P) : nullptr;
}

const IfStmt *FindRecursiveCallReturnIf(const Stmt *S,
                                        const std::string &FuncName,
                                        CallExpr *&OutCall) {
  if (!S)
    return nullptr;
  if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
    const Expr *Cond = IfS->getCond()->IgnoreParenImpCasts();
    if (const CallExpr *CE = dyn_cast<CallExpr>(Cond)) {
      if (const FunctionDecl *Callee = CE->getDirectCallee()) {
        if (Callee->getNameAsString() == FuncName) {
          if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(IfS->getThen())) {
            (void)RS;
            OutCall = const_cast<CallExpr *>(CE);
            return IfS;
          }
        }
      }
    }
  }
  for (const Stmt *Child : S->children()) {
    if (const IfStmt *Found =
            FindRecursiveCallReturnIf(Child, FuncName, OutCall))
      return Found;
  }
  return nullptr;
}
