int main() {
  DeclRefExpr target("target");
  CallExpr inner({&target});
  DeclRefExpr other("other");
  CallExpr root({&inner, &other});

  int found = ContainsDeclRefNamed(&root, "target") ? 1 : 0;
  int not_found = ContainsDeclRefNamed(&root, "missing") ? 1 : 0;
  int empty = ContainsDeclRefNamed(nullptr, "target") ? 1 : 0;

  printf("found = %d\n", found);
  printf("not_found = %d\n", not_found);
  printf("empty = %d\n", empty);
  return 0;
}
