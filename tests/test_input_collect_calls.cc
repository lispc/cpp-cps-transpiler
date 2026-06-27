// Adapted from CollectRecursiveCallsInStmt in src/transformation_rules_helpers.cc.
// Recursively walks a tree and collects nodes that match a target function id
// into an out-reference list.

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
  int kind;
  int func_id;
  Node *child_arr[2];
  int child_count;
  NodePtrList children() const {
    NodePtrList list;
    for (int i = 0; i < child_count; ++i)
      list.push_back(child_arr[i]);
    return list;
  }
};

void collect_calls(Node *node, int target_id, NodePtrList &calls) {
  if (!node)
    return;
  if (node->kind == 1 && node->func_id == target_id)
    calls.push_back(node);
  for (const auto &child : node->children()) {
    collect_calls(child, target_id, calls);
  }
}
