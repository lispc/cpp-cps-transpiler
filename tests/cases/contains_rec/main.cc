#include <iostream>
int main() {
  Node n4{1, 7, {nullptr, nullptr}, 0};
  Node n5{0, 9, {nullptr, nullptr}, 0};
  Node n2{0, 0, {&n4, &n5}, 2};
  Node n6{1, 7, {nullptr, nullptr}, 0};
  Node n3{0, 0, {&n6, nullptr}, 1};
  Node root{0, 0, {&n2, &n3}, 2};
  std::cout << "contains_rec(root, 7) = " << contains_rec(&root, 7) << std::endl;
  std::cout << "contains_rec(root, 8) = " << contains_rec(&root, 8) << std::endl;
  return 0;
}
