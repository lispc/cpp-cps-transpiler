#include <iostream>
int main() {
  for (int i = 0; i <= 6; ++i) {
    std::cout << "all_small(" << i << ") = " << (all_small(i) ? "true" : "false") << std::endl;
  }
  return 0;
}
