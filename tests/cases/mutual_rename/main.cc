#include <iostream>
int main() {
  for (int i = 0; i <= 6; ++i) {
    std::cout << "odd(" << i << ") = " << odd(i) << ", even(" << i << ") = " << even(i) << std::endl;
  }
  return 0;
}
