#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "multi_base(" << i << ") = " << multi_base(i) << std::endl;
  }
  return 0;
}
