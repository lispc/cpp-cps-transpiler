// Direct copy of IsInTailPosition (Stmt, FunctionDecl) from
// src/transformation_rules_helpers.cc.  Minimal stubs so the transpiler can
// parse it standalone; the function body itself is unchanged.

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

template <typename T>
class vector {
public:
  vector() = default;
  void push_back(T) {}
  T operator[](unsigned) const { return T(); }
  unsigned size() const { return 0; }
  bool empty() const { return true; }
  T back() const { return T(); }
};

} // namespace std

enum Opcode {
  BO_Add,
  BO_Sub,
  BO_LAnd,
  BO_LOr,
  BO_EQ,
  BO_NE,
  BO_LT,
  BO_GT,
  BO_LE,
  BO_GE
};

enum UOpcode {
  UO_LNot,
  UO_Minus
};

struct Stmt {
  virtual ~Stmt() = default;
};

struct Expr : Stmt {
  virtual const Expr *IgnoreParenImpCasts() const { return this; }
};

struct FunctionDecl {
  std::string Name;
  FunctionDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
};

struct ReturnStmt : Stmt {
  const Expr *Ret;
  ReturnStmt(const Expr *R) : Ret(R) {}
  const Expr *getRetValue() const { return Ret; }
};

struct IfStmt : Stmt {
  const Expr *Cond;
  const Stmt *Then_;
  const Stmt *Else_;
  IfStmt(const Expr *C, const Stmt *T, const Stmt *E = nullptr)
      : Cond(C), Then_(T), Else_(E) {}
  const Expr *getCond() const { return Cond; }
  const Stmt *getThen() const { return Then_; }
  const Stmt *getElse() const { return Else_; }
};

struct CompoundStmt : Stmt {
  std::vector<const Stmt *> Body;
  CompoundStmt() = default;
  void addChild(const Stmt *S) { Body.push_back(S); }
  bool body_empty() const { return Body.empty(); }
  const Stmt *body_back() const { return Body.back(); }
};

struct BinaryOperator : Expr {
  Opcode Op;
  Expr *LHS;
  Expr *RHS;
  BinaryOperator(Opcode O, Expr *L, Expr *R) : Op(O), LHS(L), RHS(R) {}
  Opcode getOpcode() const { return Op; }
  const Expr *getLHS() const { return LHS; }
  const Expr *getRHS() const { return RHS; }
};

struct UnaryOperator : Expr {
  UOpcode Op;
  Expr *Sub;
  UnaryOperator(UOpcode O, Expr *S) : Op(O), Sub(S) {}
  UOpcode getOpcode() const { return Op; }
  const Expr *getSubExpr() const { return Sub; }
};

struct CallExpr : Expr {
  FunctionDecl *Callee;
  std::vector<Expr *> Args;
  CallExpr(FunctionDecl *C) : Callee(C) {}
  void addArg(Expr *A) { Args.push_back(A); }
  const FunctionDecl *getDirectCallee() const { return Callee; }
  unsigned getNumArgs() const { return static_cast<unsigned>(Args.size()); }
  const Expr *getArg(unsigned I) const { return Args[I]; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) {
  return dynamic_cast<const T *>(P);
}

bool IsInTailPosition(const Stmt *S, const FunctionDecl *Target) {
  if (!S)
    return false;
  if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S)) {
    const Expr *E = RS->getRetValue();
    if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
      if (const FunctionDecl *Callee = CE->getDirectCallee())
        return Callee->getNameAsString() == Target->getNameAsString();
    }
    return false;
  }
  if (const IfStmt *IS = dyn_cast<IfStmt>(S)) {
    if (const Expr *Cond = IS->getCond()) {
      if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(Cond)) {
        if (BO->getOpcode() == BO_LAnd || BO->getOpcode() == BO_LOr) {
          if (IsInTailPosition(IS->getThen(), Target))
            return IsInTailPosition(IS->getElse(), Target);
        }
      }
    }
    return IsInTailPosition(IS->getThen(), Target) &&
           IsInTailPosition(IS->getElse(), Target);
  }
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    if (CS->body_empty())
      return false;
    return IsInTailPosition(CS->body_back(), Target);
  }
  return false;
}
