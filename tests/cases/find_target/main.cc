#include <iostream>
int main() {
  Node n4{1, 7, {nullptr, nullptr}, 0};
  Node n5{1, 9, {nullptr, nullptr}, 0};
  Node n2{0, 0, {&n4, &n5}, 2};
  Node n6{1, 7, {nullptr, nullptr}, 0};
  Node n3{0, 0, {&n6, nullptr}, 1};
  Node root{0, 0, {&n2, &n3}, 2};
  int out = -1;
  const Node *found = find_target(&root, 7, out);
  std::cout << "found = " << (found ? found->value : -1)
            << " out = " << out << std::endl;
  return 0;
}
