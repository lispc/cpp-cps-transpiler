#include <iostream>
int main() {
  std::cout << "C(0, 0) = " << binomial(0, 0) << std::endl;
  std::cout << "C(4, 2) = " << binomial(4, 2) << std::endl;
  std::cout << "C(5, 2) = " << binomial(5, 2) << std::endl;
  std::cout << "C(6, 3) = " << binomial(6, 3) << std::endl;
  std::cout << "C(10, 5) = " << binomial(10, 5) << std::endl;
  std::cout << "C(20, 10) = " << binomial(20, 10) << std::endl;
  return 0;
}
