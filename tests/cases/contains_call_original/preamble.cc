#include <vector>
#include <cstdio>

struct Stmt {
  virtual ~Stmt() = default;
  virtual std::vector<const Stmt *> children() const { return {}; }
};

struct CallExpr : Stmt {};

struct Node : Stmt {
  std::vector<const Stmt *> kids;
  Node(std::initializer_list<const Stmt *> ks = {}) : kids(ks) {}
  std::vector<const Stmt *> children() const override { return kids; }
};
