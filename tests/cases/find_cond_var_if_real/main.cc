#include <iostream>

int main() {
  VarDecl target;
  IfStmt node(&target);
  Stmt leaf;
  Stmt inner;
  inner.child_count = 2;
  inner.child_arr[0] = &node;
  inner.child_arr[1] = &leaf;
  Stmt root;
  root.child_count = 1;
  root.child_arr[0] = &inner;

  std::cout << "found = " << (FindCondVarIf(&target, &root) ? 1 : 0) << std::endl;
  std::cout << "not found = " << (FindCondVarIf(nullptr, &root) ? 1 : 0) << std::endl;
  return 0;
}
