#include <vector>
#include <cstdio>

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
