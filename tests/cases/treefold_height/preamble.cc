struct Tree {
  int val;
  Tree *left;
  Tree *right;
};
static Tree *node(int v, Tree *l, Tree *r) {
  Tree *t = new Tree;
  t->val = v; t->left = l; t->right = r;
  return t;
}
int max(int x, int y) { return x > y ? x : y; }
