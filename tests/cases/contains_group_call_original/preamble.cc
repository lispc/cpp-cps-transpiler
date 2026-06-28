#include <string>
#include <vector>

namespace std {

template <typename T>
class unordered_set {
public:
  static constexpr int Max = 16;
  std::string Items[Max];
  int Size;
  unordered_set() : Size(0) {}
  void insert(const std::string &V) {
    for (int i = 0; i < Size; ++i)
      if (Items[i] == V)
        return;
    Items[Size++] = V;
  }
  bool count(const std::string &V) const {
    for (int i = 0; i < Size; ++i)
      if (Items[i] == V)
        return true;
    return false;
  }
};

} // namespace std

struct StmtRange;

struct Stmt {
  virtual ~Stmt() = default;
  virtual StmtRange children() const;
};

struct StmtRange {
  static constexpr int Max = 16;
  const Stmt *Items[Max];
  int Size;
  StmtRange() : Size(0) {}
  void push_back(const Stmt *S) { Items[Size++] = S; }
  const Stmt *const *begin() const { return Items; }
  const Stmt *const *end() const { return Items + Size; }

  struct RevIt {
    const Stmt *const *P;
    const Stmt *operator*() { return *(P - 1); }
    RevIt &operator++() { --P; return *this; }
    bool operator!=(const RevIt &O) const { return P != O.P; }
  };
  RevIt rbegin() { return RevIt{Items + Size}; }
  RevIt rend() { return RevIt{Items}; }
};

StmtRange Stmt::children() const { return StmtRange(); }

struct Expr : Stmt {
  StmtRange children() const override { return StmtRange(); }
};

struct FunctionDecl {
  std::string Name;
  FunctionDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
};

struct CallExpr : Expr {
  FunctionDecl *Callee;
  StmtRange Args;
  CallExpr(FunctionDecl *C) : Callee(C) {}
  void addArg(Expr *A) { Args.push_back(A); }
  const FunctionDecl *getDirectCallee() const { return Callee; }
  unsigned getNumArgs() const { return static_cast<unsigned>(Args.Size); }
  const Expr *getArg(unsigned i) const { return static_cast<const Expr *>(Args.Items[i]); }
  StmtRange children() const override {
    StmtRange R;
    for (const Stmt *S : Args)
      R.push_back(S);
    return R;
  }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

template <typename T, typename U>
const T *dyn_cast_or_null(const U *P) {
  return P ? dynamic_cast<const T *>(P) : nullptr;
}
