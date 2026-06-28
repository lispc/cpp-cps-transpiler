#include <iostream>
int main() {
  FunctionDecl target("target"), other("other");
  Expr leaf;
  CallExpr targetCall(&target); targetCall.addArg(&leaf);
  CallExpr targetOnly(&target); targetOnly.addArg(&leaf);
  CallExpr root(&other); root.addArg(&targetCall);
  std::cout << "root = " << ContainsNonRecursiveCall(&root, "target") << std::endl;
  std::cout << "targetCall = " << ContainsNonRecursiveCall(&targetCall, "target") << std::endl;
  std::cout << "targetOnly = " << ContainsNonRecursiveCall(&targetOnly, "target") << std::endl;
  std::cout << "null = " << ContainsNonRecursiveCall(nullptr, "target") << std::endl;
  return 0;
}
