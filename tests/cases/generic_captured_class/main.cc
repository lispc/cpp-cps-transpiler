#include <iostream>
int main() {
  for (int i = 0; i <= 5; ++i) {
    std::cout << "sum_boxed(" << i << ") = " << sum_boxed(i) << std::endl;
  }
  return 0;
}
