struct Tree {
  int val;
  Tree *left;
  Tree *right;
};

int max(int x, int y) { return x > y ? x : y; }

int tree_height(Tree *t) {
  if (!t) return 0;
  return 1 + max(tree_height(t->left), tree_height(t->right));
}
