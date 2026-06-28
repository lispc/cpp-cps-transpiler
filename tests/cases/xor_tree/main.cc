#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "xor_tree(" << i << ") = " << xor_tree(i) << std::endl;
  }
  return 0;
}
