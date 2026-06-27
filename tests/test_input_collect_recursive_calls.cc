// Direct copy of CollectRecursiveCallsInStmt from src/transformation_rules_helpers.cc.
// The surrounding environment is a minimal stub so the transpiler can parse the
// file standalone; the function body itself is unchanged.

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
};

} // namespace std

struct FunctionDecl {
  std::string Name;
  FunctionDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
};

struct StmtRange;

struct Stmt {
  virtual ~Stmt() = default;
  virtual StmtRange children() const;
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

struct Expr : Stmt {};

struct CallExpr : Stmt {
  FunctionDecl *Callee;
  StmtRange Args;

  CallExpr(FunctionDecl *C) : Callee(C) {}
  void addArg(Stmt *A) { Args.push_back(A); }

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

void CollectRecursiveCallsInStmt(const Stmt *S, const std::string &FuncName,
                                 std::vector<CallExpr *> &Calls) {
  if (!S)
    return;
  if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == FuncName)
        Calls.push_back(const_cast<CallExpr *>(CE));
    }
  }
  for (const Stmt *Child : S->children())
    CollectRecursiveCallsInStmt(Child, FuncName, Calls);
}
