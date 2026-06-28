int main() {
  VarDecl vd1(1), vd2(2), vd_missing(99);

  IfStmt if1(&vd1);
  IfStmt if2(&vd2);
  Node inner({&if1, &if2});
  Node root({new Node(), &inner});

  int found_vd1 = FindCondVarIf(&vd1, &root) ? 1 : 0;
  int found_vd2 = FindCondVarIf(&vd2, &root) ? 1 : 0;
  int missing = FindCondVarIf(&vd_missing, &root) ? 1 : 0;

  printf("found_vd1 = %d\n", found_vd1);
  printf("found_vd2 = %d\n", found_vd2);
  printf("missing = %d\n", missing);
  return 0;
}
