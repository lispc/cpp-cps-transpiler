#include <string>
#include <vector>
#include <iostream>

enum Opcode { BO_LAnd, BO_LOr, BO_EQ, BO_NE, BO_LT, BO_GT, BO_LE, BO_GE, BO_Add };
enum UOpcode { UO_LNot };

struct Stmt { virtual ~Stmt() = default; };
struct Expr : Stmt { virtual const Expr *IgnoreParenImpCasts() const { return this; } };

struct ValueDecl {
  std::string Name;
  ValueDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
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

enum class EvalResult { True, False, Unknown };

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

bool ExtractParamOrLiteral(const Expr *E, const std::string &ParamName,
                           int ParamValue, int &Out) {
  E = E->IgnoreParenImpCasts();
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (DRE->getDecl()->getNameAsString() == ParamName) {
      Out = ParamValue;
      return true;
    }
  }
  if (const IntegerLiteral *IL = dyn_cast<IntegerLiteral>(E)) {
    Out = static_cast<int>(IL->getValue().getSExtValue());
    return true;
  }
  return false;
}
