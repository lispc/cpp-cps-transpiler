#include <vector>
#include <string>
#include <unordered_set>
#include <cstdio>

struct ASTContext {};

struct Expr {
  virtual ~Expr() = default;
};

struct Decl {
  virtual ~Decl() = default;
};

struct VarDecl : Decl {
  std::string Name;
  const Expr *Init_;
  VarDecl(const std::string &N, const Expr *I = nullptr) : Name(N), Init_(I) {}
  std::string getNameAsString() const { return Name; }
  const Expr *getInit() const { return Init_; }
};

struct Stmt {
  virtual ~Stmt() = default;
  virtual std::vector<const Stmt *> children() const { return {}; }
};

struct DeclStmt : Stmt {
  std::vector<const Decl *> Decls;
  void add(const Decl *D) { Decls.push_back(D); }
  const std::vector<const Decl *> &decls() const { return Decls; }
  std::vector<const Stmt *> children() const override { return {}; }
};

struct IfStmt : Stmt {
  const VarDecl *CondVar;
  std::vector<const Stmt *> Kids;
  IfStmt(const VarDecl *CV) : CondVar(CV) {}
  const VarDecl *getConditionVariable() const { return CondVar; }
  void addChild(const Stmt *S) { Kids.push_back(S); }
  std::vector<const Stmt *> children() const override { return Kids; }
};

struct Node : Stmt {
  std::vector<const Stmt *> Kids;
  void addChild(const Stmt *S) { Kids.push_back(S); }
  std::vector<const Stmt *> children() const override { return Kids; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

bool IsSubexpressionAccess(const Expr *E, const std::string &ParamName,
                           const std::unordered_set<std::string> &Derived,
                           const ASTContext *Ctx) {
  (void)E;
  (void)ParamName;
  (void)Derived;
  (void)Ctx;
  return true;
}
