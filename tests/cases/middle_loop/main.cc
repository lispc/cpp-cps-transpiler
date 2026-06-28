#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "sum_loop(" << i << ") = " << sum_loop(i) << std::endl;
  }
  return 0;
}
