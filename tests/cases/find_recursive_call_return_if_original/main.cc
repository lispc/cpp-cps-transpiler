#include <iostream>
int main() {
  FunctionDecl target("target"), other("other");
  ReturnStmt rs;
  CallExpr targetCall(&target);
  IfStmt targetIf(&targetCall, &rs);
  CallExpr otherCall(&other);
  IfStmt otherIf(&otherCall, &rs);
  Block root; root.addChild(&otherIf); root.addChild(&targetIf);
  CallExpr *out = nullptr;
  std::cout << "found = " << (FindRecursiveCallReturnIf(&root, "target", out) != nullptr) << std::endl;
  std::cout << "out_set = " << (out != nullptr) << std::endl;
  std::cout << "direct = " << (FindRecursiveCallReturnIf(&targetIf, "target", out) != nullptr) << std::endl;
  std::cout << "miss = " << (FindRecursiveCallReturnIf(&otherIf, "target", out) != nullptr) << std::endl;
  std::cout << "null = " << (FindRecursiveCallReturnIf(nullptr, "target", out) != nullptr) << std::endl;
  return 0;
}
