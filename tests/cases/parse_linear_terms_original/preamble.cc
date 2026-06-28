#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

enum Opcode { BO_Add, BO_Sub };
enum UOpcode { UO_Minus };

struct Stmt { virtual ~Stmt() = default; };
struct Expr : Stmt { virtual const Expr *IgnoreParenImpCasts() const { return this; } };

struct ValueDecl {
  std::string Name;
  ValueDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
};
struct FunctionDecl : ValueDecl {
  FunctionDecl(const std::string &N) : ValueDecl(N) {}
};

struct DeclRefExpr : Expr {
  const ValueDecl *Decl_;
  DeclRefExpr(const ValueDecl *D) : Decl_(D) {}
  const ValueDecl *getDecl() const { return Decl_; }
};

struct APInt {
  long long V;
  APInt(long long v) : V(v) {}
  long long getSExtValue() const { return V; }
};
struct IntegerLiteral : Expr {
  APInt V;
  IntegerLiteral(long long v) : V(v) {}
  const APInt &getValue() const { return V; }
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

struct LinearTerm { int Order; int Sign; CallExpr *Hole; };

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }
