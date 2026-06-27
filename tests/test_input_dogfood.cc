// Dogfood test: a simplified tree traversal inspired by
// ContainsRecursiveCall from src/transformation_rules_helpers.cc

struct Node {
  int tag;
  Node *left;
  Node *right;
};

bool contains_target(Node *n, int target) {
  if (!n)
    return false;
  if (n->tag == target)
    return true;
  return contains_target(n->left, target) || contains_target(n->right, target);
}
