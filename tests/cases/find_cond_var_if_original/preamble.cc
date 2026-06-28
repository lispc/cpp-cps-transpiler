#include <vector>
#include <cstdio>

struct VarDecl {
  int Id;
  VarDecl(int I) : Id(I) {}
};

struct Stmt {
  virtual ~Stmt() = default;
  virtual std::vector<const Stmt *> children() const { return {}; }
};

struct IfStmt : Stmt {
  const VarDecl *CondVar;
  std::vector<const Stmt *> kids;
  IfStmt(const VarDecl *V) : CondVar(V) {}
  const VarDecl *getConditionVariable() const { return CondVar; }
  std::vector<const Stmt *> children() const override { return kids; }
};

struct Node : Stmt {
  std::vector<const Stmt *> kids;
  Node(std::initializer_list<const Stmt *> ks = {}) : kids(ks) {}
  std::vector<const Stmt *> children() const override { return kids; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }
