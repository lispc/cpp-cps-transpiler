#include <iostream>
int main() {
  //      1
  //     / \
  //    2   3
  //   / \
  //  4   5
  Tree *t = node(1, node(2, node(4, nullptr, nullptr),
                           node(5, nullptr, nullptr)),
                    node(3, nullptr, nullptr));
  std::cout << tree_sum(t) << std::endl;
  std::cout << tree_sum(nullptr) << std::endl;
  std::cout << tree_sum(node(7, nullptr, nullptr)) << std::endl;
  return 0;
}
