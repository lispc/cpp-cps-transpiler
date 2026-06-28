#include <string>
#include <unordered_set>
#include <vector>

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
