int main() {
  CallExpr leaf("leaf");
  CallExpr inner("inner_func", {&leaf});
  CallExpr root("root", {&inner});

  int found = ContainsCallToPrefix(&root, "inner") ? 1 : 0;
  int not_found = ContainsCallToPrefix(&root, "missing") ? 1 : 0;
  int empty = ContainsCallToPrefix(nullptr, "inner") ? 1 : 0;

  printf("found = %d\n", found);
  printf("not_found = %d\n", not_found);
  printf("empty = %d\n", empty);
  return 0;
}
