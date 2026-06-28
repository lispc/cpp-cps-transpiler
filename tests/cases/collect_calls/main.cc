#include <iostream>
int main() {
  Node n4{1, 7, {nullptr, nullptr}, 0};
  Node n5{1, 7, {nullptr, nullptr}, 0};
  Node n2{0, 0, {&n4, &n5}, 2};
  Node n6{1, 7, {nullptr, nullptr}, 0};
  Node n3{0, 0, {&n6, nullptr}, 1};
  Node root{0, 0, {&n2, &n3}, 2};
  NodePtrList calls;
  collect_calls(&root, 7, calls);
  for (int i = 0; i < calls.size; ++i) {
    std::cout << "call[" << i << "] = " << calls.data[i]->func_id << std::endl;
  }
  return 0;
}
