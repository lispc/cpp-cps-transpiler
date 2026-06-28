#include <iostream>
int main() {
  Node n4{4, {nullptr, nullptr}, 0};
  Node n5{5, {nullptr, nullptr}, 0};
  Node n2{2, {&n4, &n5}, 2};
  Node n6{7, {nullptr, nullptr}, 0};
  Node n3{3, {&n6, nullptr}, 1};
  Node root{1, {&n2, &n3}, 2};
  std::cout << "expr_uses(root, 7) = " << expr_uses(&root, 7) << std::endl;
  std::cout << "expr_uses(root, 9) = " << expr_uses(&root, 9) << std::endl;
  return 0;
}
