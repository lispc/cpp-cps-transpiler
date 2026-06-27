// Tree traversal with an output-reference parameter.
// Pattern taken from a real AST-based traversal: collect positive node values
// into a list passed by reference.

struct TreeNode;

struct NodeList {
  TreeNode *data[10];
  int size;
  NodeList() : size(0) {}
  void push_back(TreeNode *x) { data[size++] = x; }
  TreeNode **begin() { return data; }
  TreeNode **end() { return data + size; }

  struct RevIt {
    TreeNode **p;
    TreeNode *&operator*() { return *(p - 1); }
    RevIt &operator++() { --p; return *this; }
    bool operator!=(const RevIt &o) const { return p != o.p; }
  };
  RevIt rbegin() { return RevIt{data + size}; }
  RevIt rend() { return RevIt{data}; }

  struct CRevIt {
    TreeNode *const *p;
    TreeNode *const &operator*() { return *(p - 1); }
    CRevIt &operator++() { --p; return *this; }
    bool operator!=(const CRevIt &o) const { return p != o.p; }
  };
  CRevIt rbegin() const { return CRevIt{data + size}; }
  CRevIt rend() const { return CRevIt{data}; }
};

struct IntList {
  int data[100];
  int size;
  IntList() : size(0) {}
  void push_back(int x) { data[size++] = x; }
  int operator[](int i) const { return data[i]; }
  int get_size() const { return size; }
};

struct TreeNode {
  int value;
  NodeList children;
};

void collect_holes(TreeNode *node, IntList &holes) {
  if (!node)
    return;
  if (node->value > 0)
    holes.push_back(node->value);
  for (const auto &child : node->children) {
    collect_holes(child, holes);
  }
}
