#include <vector>
#include <cstdio>

struct Stmt {
  virtual ~Stmt() = default;
};

struct Plain : Stmt {
  int Tag;
  Plain(int T) : Tag(T) {}
};

struct CompoundStmt : Stmt {
  std::vector<const Stmt *> body_;
  bool body_empty() const { return body_.empty(); }
  const std::vector<const Stmt *> &body() const { return body_; }
  void add(const Stmt *S) { body_.push_back(S); }
};

struct IfStmt : Stmt {
  const Stmt *Then_;
  const Stmt *Else_;
  IfStmt(const Stmt *T, const Stmt *E = nullptr) : Then_(T), Else_(E) {}
  const Stmt *getThen() const { return Then_; }
  const Stmt *getElse() const { return Else_; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }
