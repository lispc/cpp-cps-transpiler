#include <iostream>
int main() {
  for (int i = 0; i <= 6; ++i) {
    std::cout << "f(" << i << ") = " << f(i) << ", g(" << i << ") = " << g(i) << std::endl;
  }
  return 0;
}
