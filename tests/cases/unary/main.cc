#include <iostream>
int main() {
  for (int i = 0; i <= 6; ++i) {
    std::cout << "neg_fact(" << i << ") = " << neg_fact(i) << std::endl;
  }
  return 0;
}
