#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "xor_acc(" << i << ") = " << xor_acc(i) << std::endl;
  }
  return 0;
}
