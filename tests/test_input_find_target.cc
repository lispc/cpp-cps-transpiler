// Adapted from FindRecursiveCallReturnIf in src/transformation_rules_helpers.cc.
// Walks a tree looking for a target node; when found it stores the node's
// value through an out-reference and returns the node pointer.

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

const Node *find_target(Node *node, int target, int &out_value) {
  if (!node)
    return nullptr;
  if (node->kind == 1 && node->value == target) {
    out_value = node->value;
    return node;
  }
  for (const auto &child : node->children()) {
    if (const Node *found = find_target(child, target, out_value))
      return found;
  }
  return nullptr;
}
