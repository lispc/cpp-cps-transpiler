#include <iostream>
int main() {
  Node n4{4, {nullptr, nullptr}, 0};
  Node n5{5, {nullptr, nullptr}, 0};
  Node n6{6, {nullptr, nullptr}, 0};
  Node n7{7, {nullptr, nullptr}, 0};
  Node *c2[] = {&n4, &n5};
  Node *c3[] = {&n6, &n7};
  Node n2{2, {c2[0], c2[1]}, 2};
  Node n3{3, {c3[0], c3[1]}, 2};
  Node *croot[] = {&n2, &n3};
  Node root{1, {croot[0], croot[1]}, 2};
  std::cout << "tree_search(root, 5) = " << tree_search(&root, 5) << std::endl;
  std::cout << "tree_search(root, 8) = " << tree_search(&root, 8) << std::endl;
  std::cout << "tree_search(nullptr, 1) = " << tree_search(nullptr, 1) << std::endl;
  return 0;
}
