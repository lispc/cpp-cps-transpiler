#include <iostream>
int main() {
  ValueDecl x("x"), y("y"), z("z");
  DeclRefExpr refX(&x), refZ(&z);
  CallExpr call(nullptr); call.addArg(&refX); call.addArg(&refZ);
  std::unordered_set<std::string> params;
  params.insert("x");
  params.insert("y");
  std::cout << "refX = " << ExprUsesParams(&refX, params) << std::endl;
  std::cout << "refZ = " << ExprUsesParams(&refZ, params) << std::endl;
  std::cout << "call = " << ExprUsesParams(&call, params) << std::endl;
  std::cout << "null = " << ExprUsesParams(nullptr, params) << std::endl;
  return 0;
}
