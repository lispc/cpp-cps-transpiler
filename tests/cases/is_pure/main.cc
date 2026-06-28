#include <iostream>
int main() {
  Expr a{0, {nullptr, nullptr, nullptr}, 0};
  Expr b{0, {nullptr, nullptr, nullptr}, 0};
  Expr c{1, {nullptr, nullptr, nullptr}, 0}; // impure call
  Expr n2{2, {&a, &b, nullptr}, 2};
  Expr root1{2, {&n2, &c, nullptr}, 2};
  Expr root2{2, {&n2, &a, nullptr}, 2};
  std::cout << "is_pure(root1) = " << is_pure(&root1) << std::endl;
  std::cout << "is_pure(root2) = " << is_pure(&root2) << std::endl;
  return 0;
}
