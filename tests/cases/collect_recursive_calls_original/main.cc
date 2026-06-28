#include <iostream>
int main() {
  FunctionDecl target("target"), other("other");
  Expr leaf;
  CallExpr target1(&target); target1.addArg(&leaf);
  CallExpr target2(&target); target2.addArg(&leaf);
  CallExpr root(&other); root.addArg(&leaf); root.addArg(&target1); root.addArg(&target2);
  CallExpr otherOnly(&other); otherOnly.addArg(&leaf);
  std::vector<CallExpr *> calls1, calls2, calls3, calls4;
  CollectRecursiveCallsInStmt(&root, "target", calls1);
  CollectRecursiveCallsInStmt(&target1, "target", calls2);
  CollectRecursiveCallsInStmt(&otherOnly, "target", calls3);
  CollectRecursiveCallsInStmt(nullptr, "target", calls4);
  std::cout << "root_calls = " << calls1.size() << std::endl;
  std::cout << "target_calls = " << calls2.size() << std::endl;
  std::cout << "other_calls = " << calls3.size() << std::endl;
  std::cout << "null_calls = " << calls4.size() << std::endl;
  return 0;
}
