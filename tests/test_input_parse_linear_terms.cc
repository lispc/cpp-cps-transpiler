// Direct copy of ParseLinearTerms from src/transformation_rules_helpers.cc.
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
  bool operator!=(const string &O) const { return !(*this == O); }
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
  T *begin() { return nullptr; }
  T *end() { return nullptr; }
  const T *begin() const { return nullptr; }
  const T *end() const { return nullptr; }
  void insert(T *, T *, T *) {}
};

using size_t = unsigned long;

template <typename T>
class initializer_list {
  const T *Data;
  size_t Len;
public:
  initializer_list() : Data(nullptr), Len(0) {}
  const T *begin() const { return Data; }
  const T *end() const { return Data + Len; }
};

template <typename T>
const T &max(const T &a, const T &b) {
  return a;
}

template <typename T>
T max(std::initializer_list<T>) {
  return T();
}

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

struct FunctionDecl : ValueDecl {
  FunctionDecl(const std::string &N) : ValueDecl(N) {}
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

struct LinearTerm {
  int Order;
  int Sign;
  CallExpr *Hole;
};

template <typename T, typename U>
const T *dyn_cast(const U *P) {
  return dynamic_cast<const T *>(P);
}

bool ParseLinearTerms(const Expr *E, const std::string &FuncName,
                      const std::string &ParamName,
                      std::vector<LinearTerm> &Terms, int &MaxOrder) {
  E = E->IgnoreParenImpCasts();

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == FuncName) {
        if (CE->getNumArgs() != 1)
          return false;
        const Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();
        const BinaryOperator *BO = dyn_cast<BinaryOperator>(Arg);
        if (!BO || BO->getOpcode() != BO_Sub)
          return false;
        const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
        const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
        const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(LHS);
        if (!DRE || DRE->getDecl()->getNameAsString() != ParamName)
          return false;
        const IntegerLiteral *IL = dyn_cast<IntegerLiteral>(RHS);
        if (!IL)
          return false;
        int c = static_cast<int>(IL->getValue().getSExtValue());
        if (c <= 0)
          return false;
        Terms.push_back({c, 1, const_cast<CallExpr *>(CE)});
        MaxOrder = std::max(MaxOrder, c);
        return true;
      }
    }
  }

  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_Minus) {
      std::vector<LinearTerm> SubTerms;
      int SubMax = 0;
      if (!ParseLinearTerms(UO->getSubExpr(), FuncName, ParamName, SubTerms,
                            SubMax))
        return false;
      for (auto &t : SubTerms)
        t.Sign = -t.Sign;
      Terms.insert(Terms.end(), SubTerms.begin(), SubTerms.end());
      MaxOrder = std::max(MaxOrder, SubMax);
      return true;
    }
  }

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() != BO_Add && BO->getOpcode() != BO_Sub)
      return false;
    std::vector<LinearTerm> LeftTerms, RightTerms;
    int LeftMax = 0, RightMax = 0;
    if (!ParseLinearTerms(BO->getLHS(), FuncName, ParamName, LeftTerms, LeftMax))
      return false;
    if (!ParseLinearTerms(BO->getRHS(), FuncName, ParamName, RightTerms,
                          RightMax))
      return false;
    if (BO->getOpcode() == BO_Sub) {
      for (auto &t : RightTerms)
        t.Sign = -t.Sign;
    }
    Terms.insert(Terms.end(), LeftTerms.begin(), LeftTerms.end());
    Terms.insert(Terms.end(), RightTerms.begin(), RightTerms.end());
    MaxOrder = std::max({MaxOrder, LeftMax, RightMax});
    return true;
  }

  return false;
}
