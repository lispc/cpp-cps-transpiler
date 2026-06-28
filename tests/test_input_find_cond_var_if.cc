// Direct copy of FindCondVarIf from src/transformation_rule_tree.cc.
// Minimal Clang-AST stubs so the transpiler can parse the file standalone.

struct Stmt;

struct StmtRange {
  static constexpr int Max = 16;
  const Stmt *Items[Max];
  int Size;

  StmtRange() : Size(0) {}
  void push_back(const Stmt *S) { Items[Size++] = S; }

  const Stmt *const *begin() const { return Items; }
  const Stmt *const *end() const { return Items + Size; }

  struct CRevIt {
    const Stmt *const *P;
    const Stmt *const &operator*() { return *(P - 1); }
    CRevIt &operator++() { --P; return *this; }
    bool operator!=(const CRevIt &O) const { return P != O.P; }
  };
  CRevIt rbegin() const { return CRevIt{Items + Size}; }
  CRevIt rend() const { return CRevIt{Items}; }
};

struct VarDecl {
  int Id;
  VarDecl(int I) : Id(I) {}
};

struct Stmt {
  virtual ~Stmt() = default;
  virtual StmtRange children() const { return StmtRange(); }
};

struct IfStmt : Stmt {
  const VarDecl *CondVar;
  StmtRange Kids;
  IfStmt(const VarDecl *V) : CondVar(V) {}
  const VarDecl *getConditionVariable() const { return CondVar; }
  void addChild(const Stmt *S) { Kids.push_back(S); }
  StmtRange children() const override { return Kids; }
};

struct Node : Stmt {
  StmtRange Kids;
  void addChild(const Stmt *S) { Kids.push_back(S); }
  StmtRange children() const override { return Kids; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

const IfStmt *FindCondVarIf(const VarDecl *VD, const Stmt *Root) {
  if (!VD || !Root)
    return nullptr;
  if (const IfStmt *IfS = dyn_cast<IfStmt>(Root)) {
    if (IfS->getConditionVariable() == VD)
      return IfS;
  }
  for (const Stmt *Child : Root->children()) {
    if (const IfStmt *Found = FindCondVarIf(VD, Child))
      return Found;
  }
  return nullptr;
}
