#include <string>

struct FunctionDecl {
  std::string Name;
  bool IsConst;
  FunctionDecl(const std::string &N, bool C = false) : Name(N), IsConst(C) {}
  std::string getNameAsString() const { return Name; }
  bool isConst() const { return IsConst; }
};

struct StmtRange;

struct Stmt {
  virtual ~Stmt() = default;
  virtual StmtRange children() const;
};

enum Opcode {
  BO_Add,
  BO_Sub,
  BO_Comma,
  BO_Assign
};

enum UOpcode {
  UO_LNot,
  UO_PreInc,
  UO_PreDec,
  UO_PostInc,
  UO_PostDec
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

StmtRange Stmt::children() const { return StmtRange(); }

struct Expr : Stmt {
  virtual const Expr *IgnoreParenImpCasts() const { return this; }
  virtual StmtRange children() const override { return StmtRange(); }
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

struct BinaryOperator : Expr {
  Opcode Op;
  Expr *LHS;
  Expr *RHS;
  BinaryOperator(Opcode O, Expr *L, Expr *R) : Op(O), LHS(L), RHS(R) {}
  Opcode getOpcode() const { return Op; }
  bool isAssignmentOp() const { return Op == BO_Assign; }
  const Expr *getLHS() const { return LHS; }
  const Expr *getRHS() const { return RHS; }
  StmtRange children() const override {
    StmtRange R;
    R.push_back(LHS);
    R.push_back(RHS);
    return R;
  }
};

struct UnaryOperator : Expr {
  UOpcode Op;
  Expr *Sub;
  UnaryOperator(UOpcode O, Expr *S) : Op(O), Sub(S) {}
  UOpcode getOpcode() const { return Op; }
  bool isIncrementDecrementOp() const {
    return Op == UO_PreInc || Op == UO_PreDec ||
           Op == UO_PostInc || Op == UO_PostDec;
  }
  const Expr *getSubExpr() const { return Sub; }
  StmtRange children() const override {
    StmtRange R;
    R.push_back(Sub);
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
