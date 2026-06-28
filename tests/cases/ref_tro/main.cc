#include <iostream>
int main() {
  int y = 5;
  std::cout << "ref_accumulate(y=5, 3) = " << ref_accumulate(y, 3) << std::endl;
  std::cout << "y after = " << y << std::endl;
  return 0;
}
