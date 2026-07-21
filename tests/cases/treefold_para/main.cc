#include <iostream>
int main() {
  Tree *t = node(1, node(2, node(4, nullptr, nullptr),
                           node(5, nullptr, nullptr)),
                    node(3, nullptr, nullptr));
  std::cout << tree_para(t) << std::endl;
  std::cout << tree_para(nullptr) << std::endl;
  return 0;
}
