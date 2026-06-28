#include <iostream>
int main() {
  for (int i = 0; i <= 6; ++i) {
    std::cout << "min_tree(" << i << ") = " << min_tree(i) << std::endl;
  }
  return 0;
}
