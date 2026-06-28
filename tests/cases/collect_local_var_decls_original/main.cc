int main() {
  VarDecl a("a"), b("b"), c("c");

  DeclStmt ds1;
  ds1.add(&a);
  ds1.add(&b);

  DeclStmt ds2;
  ds2.add(&c);

  std::vector<const Stmt *> stmts;
  stmts.push_back(&ds1);
  stmts.push_back(&ds2);

  std::vector<const VarDecl *> out;
  CollectLocalVarDecls(stmts, out);

  int has_a = 0, has_b = 0, has_c = 0;
  for (const VarDecl *VD : out) {
    if (VD->getNameAsString() == "a") has_a = 1;
    if (VD->getNameAsString() == "b") has_b = 1;
    if (VD->getNameAsString() == "c") has_c = 1;
  }

  printf("has_a = %d\n", has_a);
  printf("has_b = %d\n", has_b);
  printf("has_c = %d\n", has_c);
  return 0;
}
