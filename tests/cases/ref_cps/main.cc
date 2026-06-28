#include <iostream>
int main() {
  int x = 5;
  std::cout << "ref_sum(x=5, 3) = " << ref_sum(x, 3) << std::endl;
  std::cout << "ref_sum(x=5, 5) = " << ref_sum(x, 5) << std::endl;
  return 0;
}
