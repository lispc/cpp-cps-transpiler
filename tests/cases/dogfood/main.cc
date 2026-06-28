#include <iostream>
int main() {
  Node n4{4, nullptr, nullptr};
  Node n5{5, nullptr, nullptr};
  Node n6{6, nullptr, nullptr};
  Node n7{7, nullptr, nullptr};
  Node n2{2, &n4, &n5};
  Node n3{3, &n6, &n7};
  Node root{1, &n2, &n3};
  std::cout << "contains_target(root, 5) = " << contains_target(&root, 5) << std::endl;
  std::cout << "contains_target(root, 8) = " << contains_target(&root, 8) << std::endl;
  std::cout << "contains_target(nullptr, 1) = " << contains_target(nullptr, 1) << std::endl;
  return 0;
}
