// Copy of FlattenIfElse from src/cps_generator.cc.
// Minimal stubs for the AST / BodyAnalysis API used by the function.

namespace std {

template <typename T>
class vector {
public:
  static constexpr int Max = 16;
  T Items[Max];
  int Size;
  vector() : Size(0) {}
  void push_back(const T &V) { Items[Size++] = V; }
  int size() const { return Size; }
  T operator[](int i) const { return Items[i]; }
};

} // namespace std

struct ASTContext {};
struct Expr { int Tag; };
struct Stmt { virtual ~Stmt() = default; };

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

struct BaseCase {
  const Expr *CondExpr;
  const Expr *ValueExpr;
};

struct BodyAnalysis {
  std::vector<BaseCase> BaseCases;
  const Expr *RecExpr;
  bool IsRecursive;
  BodyAnalysis() : RecExpr(nullptr), IsRecursive(false) {}
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

const Expr *ExtractReturnExpr(const Stmt *S) {
  if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S))
    return RS->getRetValue();
  return nullptr;
}

BaseCase MakeBaseCase(const Expr *Cond, const Expr *Value,
                      const ASTContext *Ctx) {
  (void)Ctx;
  BaseCase bc;
  bc.CondExpr = Cond;
  bc.ValueExpr = Value;
  return bc;
}

void FlattenIfElse(const Stmt *S, BodyAnalysis &BA, const ASTContext *Ctx) {
  if (!S)
    return;
  if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
    const Expr *BaseExpr = ExtractReturnExpr(IfS->getThen());
    if (BaseExpr)
      BA.BaseCases.push_back(MakeBaseCase(IfS->getCond(), BaseExpr, Ctx));
    if (const Stmt *Else = IfS->getElse())
      FlattenIfElse(Else, BA, Ctx);
    return;
  }
  if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S)) {
    BA.RecExpr = RS->getRetValue();
    BA.IsRecursive = true;
  }
}
