#include <string>

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
