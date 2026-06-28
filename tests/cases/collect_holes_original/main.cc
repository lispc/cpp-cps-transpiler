#include <iostream>
int main() {
  FunctionDecl target("target"), other("other");
  Expr leaf1, leaf2;
  CallExpr inner1(&target); inner1.addArg(&leaf1);
  CallExpr inner2(&target); inner2.addArg(&leaf2);
  CallExpr outer(&other); outer.addArg(&inner1); outer.addArg(&inner2);
  CallExpr otherOnly(&other); otherOnly.addArg(&leaf1);
  std::vector<CallExpr *> holes1, holes2, holes3, holes4;
  CollectHoles(&outer, "target", holes1);
  CollectHoles(&inner1, "target", holes2);
  CollectHoles(&otherOnly, "target", holes3);
  CollectHoles(nullptr, "target", holes4);
  std::cout << "outer_holes = " << holes1.size() << std::endl;
  std::cout << "inner_holes = " << holes2.size() << std::endl;
  std::cout << "other_holes = " << holes3.size() << std::endl;
  std::cout << "null_holes = " << holes4.size() << std::endl;
  return 0;
}
