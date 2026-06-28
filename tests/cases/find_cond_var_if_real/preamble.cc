// 为生成的迭代版 FindCondVarIf 提供类型定义（与输入文件中的 stub 相同）。

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
