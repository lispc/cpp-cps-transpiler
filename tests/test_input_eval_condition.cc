// Direct copy of EvalConditionForParam from src/transformation_rules_helpers.cc.
// Minimal stubs so the transpiler can parse it standalone.

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

struct IntegerLiteral : Expr {
  long long V;
  IntegerLiteral(long long v) : V(v) {}
  long long getValue() const { return V; }
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
const T *dyn_cast(const U *P) {
  return dynamic_cast<const T *>(P);
}

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
    Out = static_cast<int>(IL->getValue());
    return true;
  }
  return false;
}

EvalResult EvalConditionForParam(const Expr *E, const std::string &ParamName,
                                 int ParamValue) {
  E = E->IgnoreParenImpCasts();

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BO_LAnd) {
      auto L = EvalConditionForParam(BO->getLHS(), ParamName, ParamValue);
      auto R = EvalConditionForParam(BO->getRHS(), ParamName, ParamValue);
      if (L == EvalResult::False || R == EvalResult::False)
        return EvalResult::False;
      if (L == EvalResult::Unknown || R == EvalResult::Unknown)
        return EvalResult::Unknown;
      return EvalResult::True;
    }
    if (BO->getOpcode() == BO_LOr) {
      auto L = EvalConditionForParam(BO->getLHS(), ParamName, ParamValue);
      auto R = EvalConditionForParam(BO->getRHS(), ParamName, ParamValue);
      if (L == EvalResult::True || R == EvalResult::True)
        return EvalResult::True;
      if (L == EvalResult::Unknown || R == EvalResult::Unknown)
        return EvalResult::Unknown;
      return EvalResult::False;
    }

    int lhsVal = 0, rhsVal = 0;
    bool lhsKnown =
        ExtractParamOrLiteral(BO->getLHS(), ParamName, ParamValue, lhsVal);
    bool rhsKnown =
        ExtractParamOrLiteral(BO->getRHS(), ParamName, ParamValue, rhsVal);
    if (!lhsKnown || !rhsKnown)
      return EvalResult::Unknown;

    switch (BO->getOpcode()) {
    case BO_EQ:
      return lhsVal == rhsVal ? EvalResult::True : EvalResult::False;
    case BO_NE:
      return lhsVal != rhsVal ? EvalResult::True : EvalResult::False;
    case BO_LT:
      return lhsVal < rhsVal ? EvalResult::True : EvalResult::False;
    case BO_GT:
      return lhsVal > rhsVal ? EvalResult::True : EvalResult::False;
    case BO_LE:
      return lhsVal <= rhsVal ? EvalResult::True : EvalResult::False;
    case BO_GE:
      return lhsVal >= rhsVal ? EvalResult::True : EvalResult::False;
    default:
      return EvalResult::Unknown;
    }
  }

  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_LNot) {
      auto R = EvalConditionForParam(UO->getSubExpr(), ParamName, ParamValue);
      if (R == EvalResult::True)
        return EvalResult::False;
      if (R == EvalResult::False)
        return EvalResult::True;
      return EvalResult::Unknown;
    }
  }

  return EvalResult::Unknown;
}
