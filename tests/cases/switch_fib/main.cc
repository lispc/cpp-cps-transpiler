#include <iostream>
int main() {
  for (int i = 0; i <= 10; ++i) {
    std::cout << "fib_switch(" << i << ") = " << fib_switch(i) << std::endl;
  }
  return 0;
}
