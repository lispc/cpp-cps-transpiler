#include <iostream>
int main() {
  for (int i = -3; i <= 8; ++i) {
    std::cout << "fact_guard(" << i << ") = " << fact_guard(i) << std::endl;
  }
  return 0;
}
