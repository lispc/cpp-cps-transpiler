#include <iostream>
int main() {
  FunctionDecl target("target"), other("other");
  Expr leaf1, leaf2;
  CallExpr arg1(&target); arg1.addArg(&leaf1);
  CallExpr arg2(&target); arg2.addArg(&leaf2);
  CallExpr root(&other); root.addArg(&arg1); root.addArg(&arg2);
  std::vector<CallExpr *> holes;
  CollectHolesDeep(&root, "target", holes);
  std::cout << "count = " << holes.size() << std::endl;
  std::cout << "order_ok = " << (holes.size() == 2 && holes[0] == &arg1 && holes[1] == &arg2 ? 1 : 0) << std::endl;
  std::vector<CallExpr *> empty;
  CollectHolesDeep(nullptr, "target", empty);
  std::cout << "null_count = " << empty.size() << std::endl;
  return 0;
}
