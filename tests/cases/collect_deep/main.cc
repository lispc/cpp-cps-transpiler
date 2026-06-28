#include <iostream>
int main() {
  Node n4{1, 4, {nullptr, nullptr}, 0};
  Node n5{0, 5, {nullptr, nullptr}, 0};
  Node n2{0, 2, {&n4, &n5}, 2};
  Node n6{1, 6, {nullptr, nullptr}, 0};
  Node n3{0, 3, {&n6, nullptr}, 1};
  Node root{0, 1, {&n2, &n3}, 2};
  NodePtrList calls;
  collect_deep(&root, calls);
  for (int i = 0; i < calls.size; ++i) {
    std::cout << "call[" << i << "] = " << calls.data[i]->value << std::endl;
  }
  return 0;
}
