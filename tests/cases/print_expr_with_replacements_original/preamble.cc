#include <cstdio>

namespace std {

class string {
  static constexpr int Max = 1024;
  char Data[Max];
  int Len;
public:
  string() { Data[0] = '\0'; Len = 0; }
  string(const char *S) {
    Len = 0;
    while (S[Len] && Len < Max - 1) { Data[Len] = S[Len]; ++Len; }
    Data[Len] = '\0';
  }
  string(const string &O) {
    Len = O.Len;
    for (int i = 0; i <= Len; ++i) Data[i] = O.Data[i];
  }
  string &operator=(const string &O) {
    Len = O.Len;
    for (int i = 0; i <= Len; ++i) Data[i] = O.Data[i];
    return *this;
  }
  string &operator+=(const string &O) {
    for (int i = 0; i < O.Len && Len < Max - 1; ++i)
      Data[Len++] = O.Data[i];
    Data[Len] = '\0';
    return *this;
  }
  string &operator+=(const char *S) {
    int i = 0;
    while (S[i] && Len < Max - 1)
      Data[Len++] = S[i++];
    Data[Len] = '\0';
    return *this;
  }
  friend string operator+(const string &A, const string &B) {
    string R = A; R += B; return R;
  }
  friend string operator+(const string &A, const char *B) {
    string R = A; R += B; return R;
  }
  friend string operator+(const char *A, const string &B) {
    string R = A; R += B; return R;
  }
  bool operator==(const string &O) const {
    if (Len != O.Len) return false;
    for (int i = 0; i < Len; ++i)
      if (Data[i] != O.Data[i]) return false;
    return true;
  }
  bool operator!=(const string &O) const { return !(*this == O); }
  int size() const { return Len; }
  const char *c_str() const { return Data; }
};

template <typename A, typename B>
struct pair {
  A first;
  B second;
  pair() = default;
  pair(const A &a, const B &b) : first(a), second(b) {}
};

template <typename K, typename V>
class unordered_map {
public:
  static constexpr int Max = 16;
  pair<K, V> Items[Max];
  int Size;
  unordered_map() : Size(0) {}

  struct iterator {
    pair<K, V> *Items;
    int Idx;
    iterator(pair<K, V> *items, int idx) : Items(items), Idx(idx) {}
    pair<K, V> &operator*() const { return Items[Idx]; }
    pair<K, V> *operator->() const { return &Items[Idx]; }
    iterator &operator++() { ++Idx; return *this; }
    bool operator!=(const iterator &O) const { return Idx != O.Idx; }
  };

  iterator find(const K &Key) const {
    for (int i = 0; i < Size; ++i) {
      if (Items[i].first == Key)
        return iterator(const_cast<pair<K, V>*>(&Items[i]), i);
    }
    return end();
  }

  iterator end() const { return iterator(nullptr, Size); }

  V &operator[](const K &Key) {
    for (int i = 0; i < Size; ++i) {
      if (Items[i].first == Key)
        return Items[i].second;
    }
    Items[Size].first = Key;
    Items[Size].second = V();
    return Items[Size++].second;
  }
};

template <typename T>
class vector {
public:
  static constexpr int Max = 256;
  T Items[Max];
  int Size;
  vector() : Size(0) {}
  explicit vector(unsigned n) : Size(static_cast<int>(n)) {
    for (int i = 0; i < Size; ++i) Items[i] = T();
  }
  void push_back(const T &V) { Items[Size++] = V; }
  void pop_back() { --Size; }
  T &back() { return Items[Size - 1]; }
  const T &back() const { return Items[Size - 1]; }
  T &operator[](int i) { return Items[i]; }
  const T &operator[](int i) const { return Items[i]; }
  bool empty() const { return Size == 0; }
};

} // namespace std

struct ASTContext {};

struct Stmt {
  virtual ~Stmt() = default;
};

struct Expr : Stmt {};

struct StringRefLike {
  std::string S;
  StringRefLike(const std::string &s = "") : S(s) {}
  std::string str() const { return S; }
};

struct TypeInfo {
  std::string Name;
  TypeInfo(const std::string &n = "") : Name(n) {}
  std::string getAsString() const { return Name; }
};

struct MemberNameInfo {
  std::string Name;
  MemberNameInfo(const std::string &n = "") : Name(n) {}
  std::string getAsString() const { return Name; }
};

enum Opcode { BO_Add };
enum UOpcode { UO_PreInc, UO_PostInc };

struct BinaryOperator : Expr {
  Opcode Op;
  Expr *LHS;
  Expr *RHS;
  BinaryOperator(Expr *L, Expr *R) : Op(BO_Add), LHS(L), RHS(R) {}
  const Expr *getLHS() const { return LHS; }
  const Expr *getRHS() const { return RHS; }
  StringRefLike getOpcodeStr() const { return StringRefLike("+"); }
};

struct UnaryOperator : Expr {
  UOpcode Op;
  Expr *Sub;
  UnaryOperator(UOpcode o, Expr *s) : Op(o), Sub(s) {}
  UOpcode getOpcode() const { return Op; }
  const Expr *getSubExpr() const { return Sub; }
  bool isPostfix() const { return Op == UO_PostInc; }
  StringRefLike getOpcodeStr(UOpcode) const {
    return StringRefLike("++");
  }
};

struct CallExpr : Expr {
  Expr *CalleeExpr;
  Expr *Args[8];
  unsigned NumArgs;
  CallExpr(Expr *callee, unsigned n = 0) : CalleeExpr(callee), NumArgs(n) {
    for (unsigned i = 0; i < 8; ++i) Args[i] = nullptr;
  }
  void setArg(unsigned i, Expr *a) { Args[i] = a; }
  const Expr *getCallee() const { return CalleeExpr; }
  unsigned getNumArgs() const { return NumArgs; }
  const Expr *getArg(unsigned i) const { return Args[i]; }
};

struct ConditionalOperator : Expr {
  Expr *Cond;
  Expr *True;
  Expr *False;
  ConditionalOperator(Expr *c, Expr *t, Expr *f)
      : Cond(c), True(t), False(f) {}
  const Expr *getCond() const { return Cond; }
  const Expr *getTrueExpr() const { return True; }
  const Expr *getFalseExpr() const { return False; }
};

struct ArraySubscriptExpr : Expr {
  Expr *Base_;
  Expr *Idx_;
  ArraySubscriptExpr(Expr *b, Expr *i) : Base_(b), Idx_(i) {}
  const Expr *getBase() const { return Base_; }
  const Expr *getIdx() const { return Idx_; }
};

struct MemberExpr : Expr {
  Expr *Base_;
  bool Arrow;
  MemberNameInfo Info;
  MemberExpr(Expr *b, bool a, const std::string &n)
      : Base_(b), Arrow(a), Info(n) {}
  const Expr *getBase() const { return Base_; }
  bool isArrow() const { return Arrow; }
  MemberNameInfo getMemberNameInfo() const { return Info; }
};

struct ParenExpr : Expr {
  Expr *Sub;
  ParenExpr(Expr *s) : Sub(s) {}
  const Expr *getSubExpr() const { return Sub; }
};

struct ImplicitCastExpr : Expr {
  Expr *Sub;
  ImplicitCastExpr(Expr *s) : Sub(s) {}
  const Expr *getSubExpr() const { return Sub; }
};

struct CStyleCastExpr : Expr {
  TypeInfo Type;
  Expr *Sub;
  CStyleCastExpr(const std::string &t, Expr *s) : Type(t), Sub(s) {}
  TypeInfo getTypeAsWritten() const { return Type; }
  const Expr *getSubExpr() const { return Sub; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) {
  return dynamic_cast<const T *>(P);
}

std::string PrintExpr(const Expr *E, const ASTContext *Ctx) {
  return "<expr>";
}
