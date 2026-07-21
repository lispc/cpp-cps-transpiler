#include <iostream>
int main() {
  Tree *t = node(1, node(2, node(4, nullptr, nullptr),
                           node(5, nullptr, nullptr)),
                    node(3, nullptr, nullptr));
  std::cout << tree_height(t) << std::endl;
  std::cout << tree_height(nullptr) << std::endl;
  std::cout << tree_height(node(7, nullptr, nullptr)) << std::endl;
  return 0;
}
