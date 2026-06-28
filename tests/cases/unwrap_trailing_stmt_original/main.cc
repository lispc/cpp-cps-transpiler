int main() {
  Plain p1(1), p2(2);

  CompoundStmt cs;
  cs.add(&p1);
  cs.add(&p2);

  IfStmt if_no_else(&cs);
  IfStmt if_with_else(&p1, &p2);

  const Stmt *r1 = UnwrapTrailingStmt(&p1);
  const Stmt *r2 = UnwrapTrailingStmt(&if_no_else);
  const Stmt *r3 = UnwrapTrailingStmt(&if_with_else);

  int plain_ok = (r1 == &p1) ? 1 : 0;
  int nested_ok = (r2 == &p2) ? 1 : 0;
  int else_ok = (r3 == &if_with_else) ? 1 : 0;

  printf("plain_ok = %d\n", plain_ok);
  printf("nested_ok = %d\n", nested_ok);
  printf("else_ok = %d\n", else_ok);
  return 0;
}
