int main() {
  CallExpr target;
  CallExpr orphan;
  CallExpr missing;

  Node inner({&orphan});
  Node root({&inner, &target});

  int found = ContainsCall(&root, &target) ? 1 : 0;
  int not_found = ContainsCall(&root, &missing) ? 1 : 0;
  int empty = ContainsCall(nullptr, &target) ? 1 : 0;

  printf("found = %d\n", found);
  printf("not_found = %d\n", not_found);
  printf("empty = %d\n", empty);
  return 0;
}
