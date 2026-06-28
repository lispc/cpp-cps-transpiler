#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "acc_multi_base(" << i << ") = " << acc_multi_base(i) << std::endl;
  }
  return 0;
}
