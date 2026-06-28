// 直接从 src/transformation_rule_tree.cc 拷出来的真实递归函数 ContainsCall，
// 仅补充了可独立编译的轻量 Stmt/CallExpr stub，函数体本身未作任何改动。

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

struct CallExpr : Stmt {};

bool ContainsCall(const Stmt *Root, const CallExpr *Target) {
  if (!Root)
    return false;
  if (Root == Target)
    return true;
  for (const Stmt *Child : Root->children()) {
    if (ContainsCall(Child, Target))
      return true;
  }
  return false;
}
