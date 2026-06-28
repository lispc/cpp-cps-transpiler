// 直接从 src/transformation_rule_tree.cc 拷出来的真实递归函数 FindCondVarIf，
// 仅补充了可独立编译的轻量 stub，函数体本身未作任何改动。

struct Stmt;

struct ChildrenList {
  const Stmt *items[4];
  int n;
  const Stmt *const *begin() const { return items; }
  const Stmt *const *end() const { return items + n; }

  struct RevIt {
    const Stmt *const *p;
    const Stmt *operator*() const { return *(p - 1); }
    RevIt &operator++() { --p; return *this; }
    bool operator!=(const RevIt &o) const { return p != o.p; }
  };
  RevIt rbegin() const { return RevIt{items + n}; }
  RevIt rend() const { return RevIt{items}; }
};

struct Stmt {
  Stmt *child_arr[4];
  int child_count;
  Stmt() : child_count(0) {}
  virtual ~Stmt() = default;
  ChildrenList children() const {
    ChildrenList list;
    list.n = child_count;
    for (int i = 0; i < child_count; ++i)
      list.items[i] = child_arr[i];
    return list;
  }
};

struct VarDecl {};

struct IfStmt : Stmt {
  VarDecl *cond_var;
  IfStmt(VarDecl *v) : cond_var(v) {}
  VarDecl *getConditionVariable() const { return cond_var; }
};

template <typename T>
const T *dyn_cast(const Stmt *S) {
  return dynamic_cast<const T *>(S);
}

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
