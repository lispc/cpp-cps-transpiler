#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "with_local(" << i << ") = " << with_local(i) << std::endl;
  }
  return 0;
}
