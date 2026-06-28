#include <iostream>
int main() {
  FunctionDecl target("target"), other("other");
  Expr leaf1, leaf2;
  CallExpr direct(&target); direct.addArg(&leaf1);
  CallExpr nested(&other); nested.addArg(&direct);
  CallExpr otherOnly(&other); otherOnly.addArg(&leaf1);
  std::cout << "direct = " << ContainsRecursiveCall(&direct, "target") << std::endl;
  std::cout << "nested = " << ContainsRecursiveCall(&nested, "target") << std::endl;
  std::cout << "otherOnly = " << ContainsRecursiveCall(&otherOnly, "target") << std::endl;
  std::cout << "null = " << ContainsRecursiveCall(nullptr, "target") << std::endl;
  return 0;
}
