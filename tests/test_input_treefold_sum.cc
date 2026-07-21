struct Tree {
  int val;
  Tree *left;
  Tree *right;
};

int tree_sum(Tree *t) {
  if (!t) return 0;
  return t->val + tree_sum(t->left) + tree_sum(t->right);
}
