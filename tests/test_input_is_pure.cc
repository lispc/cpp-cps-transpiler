// Adapted from IsPureExprImpl in src/transformation_rules_helpers.cc.
// Simplified to a single recursive call site: it checks whether an expression
// tree is pure by verifying that no call-node appears and all arguments are
// also pure.

struct Expr {
  int kind;        // 0 = pure leaf, 1 = impure call, 2 = node with children
  Expr *args[3];
  int arg_count;
};

bool is_pure(Expr *e) {
  if (!e)
    return true;
  if (e->kind == 1)
    return false;
  for (int i = 0; i < e->arg_count; ++i) {
    if (!is_pure(e->args[i]))
      return false;
  }
  return true;
}
