struct Tree {
  int val;
  Tree *left;
  Tree *right;
};

// Paramorphism: the combine expression references the child node itself
// (t->left->val), not just the recursive results.
int tree_para(Tree *t) {
  if (!t) return 0;
  return (t->left ? t->left->val : 0) + tree_para(t->left) +
         tree_para(t->right);
}
