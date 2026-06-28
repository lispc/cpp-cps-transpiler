#include <iostream>

int main() {
  CallExpr target;
  Stmt leaf;
  Stmt inner;
  inner.child_count = 2;
  inner.child_arr[0] = &leaf;
  inner.child_arr[1] = &target;
  Stmt root;
  root.child_count = 1;
  root.child_arr[0] = &inner;

  std::cout << "contains target = " << ContainsCall(&root, &target) << std::endl;
  std::cout << "contains null = " << ContainsCall(&root, nullptr) << std::endl;
  std::cout << "null root = " << ContainsCall(nullptr, &target) << std::endl;
  return 0;
}
