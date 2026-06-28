#include <iostream>
int main() {
  for (int i = 0; i <= 10; ++i) {
    std::cout << "double_fact(" << i << ") = " << double_fact(i) << std::endl;
  }
  return 0;
}
