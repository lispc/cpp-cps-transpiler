#include <iostream>
int main() {
  for (int i = 0; i <= 4; ++i) {
    std::cout << "nested_fact(" << i << ") = " << nested_fact(i) << std::endl;
  }
  return 0;
}
