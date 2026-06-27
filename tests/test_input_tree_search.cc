// Tree-traversal recursion: search a node with children stored in an array.

struct Node {
  int tag;
  Node *children[2];
  int num_children;
};

bool tree_search(Node *n, int target) {
  if (!n)
    return false;
  if (n->tag == target)
    return true;
  for (int i = 0; i < n->num_children; ++i) {
    if (tree_search(n->children[i], target))
      return true;
  }
  return false;
}
