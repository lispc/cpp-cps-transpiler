int main() {
  CallExpr leaf("leaf");
  CallExpr inner("recurse", {&leaf});
  CallExpr root("root", {&inner});

  const CallExpr *found = FindDirectRecursiveCallInExpr(&root, "recurse");
  const CallExpr *not_found = FindDirectRecursiveCallInExpr(&root, "missing");
  const CallExpr *empty = FindDirectRecursiveCallInExpr(nullptr, "recurse");

  printf("found = %d\n", found ? 1 : 0);
  printf("not_found = %d\n", not_found ? 1 : 0);
  printf("empty = %d\n", empty ? 1 : 0);
  return 0;
}
