// Adapted from CollectHolesDeep in src/transformation_rules_helpers.cc.
// Performs a post-order tree walk: children are visited first, then matching
// nodes are recorded.  The recursive call inside the loop is guarded by an
// if (null) check.

struct Node;

struct NodePtrList {
  Node *data[10];
  int size;
  NodePtrList() : size(0) {}
  void push_back(Node *x) { data[size++] = x; }
  Node **begin() { return data; }
  Node **end() { return data + size; }

  struct RevIt {
    Node **p;
    Node *&operator*() { return *(p - 1); }
    RevIt &operator++() { --p; return *this; }
    bool operator!=(const RevIt &o) const { return p != o.p; }
  };
  RevIt rbegin() { return RevIt{data + size}; }
  RevIt rend() { return RevIt{data}; }

  struct CRevIt {
    Node *const *p;
    Node *const &operator*() { return *(p - 1); }
    CRevIt &operator++() { --p; return *this; }
    bool operator!=(const CRevIt &o) const { return p != o.p; }
  };
  CRevIt rbegin() const { return CRevIt{data + size}; }
  CRevIt rend() const { return CRevIt{data}; }
};

struct Node {
  int kind;        // 1 = call node to be recorded
  int value;
  Node *child_arr[2];
  int child_count;
  NodePtrList children() const {
    NodePtrList list;
    for (int i = 0; i < child_count; ++i)
      list.push_back(child_arr[i]);
    return list;
  }
};

void collect_deep(Node *node, NodePtrList &calls) {
  if (!node)
    return;
  for (const auto &child : node->children()) {
    if (child)
      collect_deep(child, calls);
  }
  if (node->kind == 1)
    calls.push_back(node);
}
