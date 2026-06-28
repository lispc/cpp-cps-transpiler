int main() {
  ASTContext ctx;
  std::unordered_set<std::string> derived;
  Expr init;

  VarDecl a("a", &init);
  VarDecl b("b", &init);
  VarDecl c("c", &init);
  VarDecl d("d", &init);

  DeclStmt ds1;
  ds1.add(&a);
  ds1.add(&b);

  DeclStmt ds2;
  ds2.add(&d);

  IfStmt ifs(&c);
  ifs.addChild(&ds2);

  Node root;
  root.addChild(&ds1);
  root.addChild(&ifs);

  CollectDerivedVariables(&root, std::string("base"), derived, &ctx);

  int has_a = derived.count("a") ? 1 : 0;
  int has_b = derived.count("b") ? 1 : 0;
  int has_c = derived.count("c") ? 1 : 0;
  int has_d = derived.count("d") ? 1 : 0;

  printf("has_a = %d\n", has_a);
  printf("has_b = %d\n", has_b);
  printf("has_c = %d\n", has_c);
  printf("has_d = %d\n", has_d);
  return 0;
}
