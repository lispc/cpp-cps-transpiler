#include <iostream>
int main() {
  std::cout << "mc91(50) = " << mc91(50) << std::endl;
  std::cout << "mc91(101) = " << mc91(101) << std::endl;
  std::cout << "mc91(110) = " << mc91(110) << std::endl;
  for (int i = 0; i <= 8; ++i) {
    std::cout << "mod0(" << i << ") = " << mod0(i) << ", mod1(" << i << ") = " << mod1(i) << ", mod2(" << i << ") = " << mod2(i) << std::endl;
  }
  return 0;
}
