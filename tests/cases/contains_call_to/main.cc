int main() {
  CallExpr leaf("leaf");
  CallExpr inner("inner", {&leaf});
  CallExpr root("root", {&inner});

  int found = ContainsCallTo(&root, "inner") ? 1 : 0;
  int not_found = ContainsCallTo(&root, "missing") ? 1 : 0;
  int empty = ContainsCallTo(nullptr, "inner") ? 1 : 0;

  printf("found = %d\n", found);
  printf("not_found = %d\n", not_found);
  printf("empty = %d\n", empty);
  return 0;
}
