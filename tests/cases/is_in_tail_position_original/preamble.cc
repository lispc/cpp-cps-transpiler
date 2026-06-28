#include <string>
#include <vector>
#include <iostream>

enum Opcode { BO_LAnd, BO_LOr };

struct Stmt { virtual ~Stmt() = default; };
struct Expr : Stmt { virtual const Expr *IgnoreParenImpCasts() const { return this; } };

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
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }
