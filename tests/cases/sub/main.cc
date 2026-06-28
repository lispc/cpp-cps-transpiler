#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "sub_acc(" << i << ") = " << sub_acc(i) << std::endl;
  }
  return 0;
}
