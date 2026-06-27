// Copy of IsInTailPosition(const Expr*, const Stmt*, const std::string&)
// from src/transformation_rules_helpers.cc.
// Minimal stubs for the standard library / AST API used by the function.

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

struct Stmt { virtual ~Stmt() = default; };
struct Expr : Stmt {};

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
  const Stmt *getThen() const { return Then_; }
  const Stmt *getElse() const { return Else_; }
};

struct StmtRange {
  const Stmt *const *Begin;
  const Stmt *const *End;
  const Stmt *const *begin() const { return Begin; }
  const Stmt *const *end() const { return End; }
};

struct CompoundStmt : Stmt {
  static constexpr int Max = 16;
  const Stmt *Items[Max];
  int Size;
  CompoundStmt() : Size(0) {}
  void addChild(const Stmt *S) { Items[Size++] = S; }
  bool body_empty() const { return Size == 0; }
  StmtRange body() const { return StmtRange{Items, Items + Size}; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

bool IsInTailPosition(const Expr *E, const Stmt *S,
                      const std::string &FuncName) {
  if (!E || !S)
    return false;
  if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S))
    return RS->getRetValue() == E;
  // For void tail-recursive functions, the recursive call may appear as the
  // final expression statement (not wrapped in a return).
  if (const Expr *ExprS = dyn_cast<Expr>(S))
    return ExprS == E;
  if (const IfStmt *IfS = dyn_cast<IfStmt>(S))
    return IsInTailPosition(E, IfS->getThen(), FuncName) ||
           IsInTailPosition(E, IfS->getElse(), FuncName);
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    if (CS->body_empty())
      return false;
    const Stmt *Last = nullptr;
    for (const Stmt *Child : CS->body())
      Last = Child;
    return IsInTailPosition(E, Last, FuncName);
  }
  return false;
}
