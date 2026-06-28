#include <iostream>
int main() {
  ValueDecl target, other;
  DeclRefExpr refTarget(&target);
  DeclRefExpr refOther(&other);
  CallExpr wrapper;
  wrapper.addArg(&refTarget);
  CallExpr wrapperOther;
  wrapperOther.addArg(&refOther);
  std::cout << "direct = " << ExprContainsDeclRef(&refTarget, &target) << std::endl;
  std::cout << "nested = " << ExprContainsDeclRef(&wrapper, &target) << std::endl;
  std::cout << "other = " << ExprContainsDeclRef(&wrapperOther, &target) << std::endl;
  std::cout << "null = " << ExprContainsDeclRef(nullptr, &target) << std::endl;
  return 0;
}
