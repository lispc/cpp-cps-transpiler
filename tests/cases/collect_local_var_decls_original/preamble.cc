#include <vector>
#include <string>
#include <cstdio>

struct Decl {
  virtual ~Decl() = default;
};

struct VarDecl : Decl {
  std::string Name;
  VarDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
};

struct Stmt {
  virtual ~Stmt() = default;
};

struct DeclStmt : Stmt {
  std::vector<const Decl *> Decls;
  void add(const Decl *D) { Decls.push_back(D); }
  const std::vector<const Decl *> &decls() const { return Decls; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

void CollectLocalVarDecls(const Stmt *S,
                          std::vector<const VarDecl *> &Out) {
  if (!S)
    return;
  if (const DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : DS->decls()) {
      if (const VarDecl *VD = dyn_cast<VarDecl>(D))
        Out.push_back(VD);
    }
  }
}

void CollectLocalVarDecls(const std::vector<const Stmt *> &Stmts,
                          std::vector<const VarDecl *> &Out) {
  for (const Stmt *S : Stmts)
    CollectLocalVarDecls(S, Out);
}
