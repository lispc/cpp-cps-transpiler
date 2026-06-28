#include <iostream>
int main() {
  print_down(5);
  for (int i = 0; i < emitted_count; ++i) {
    std::cout << "emit[" << i << "] = " << emitted[i] << std::endl;
  }
  return 0;
}
