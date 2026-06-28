#include <iostream>
int main() {
  std::cout << "fib_ll(10) = " << fib_ll(10) << std::endl;
  std::cout << "fact_unsigned(5) = " << fact_unsigned(5) << std::endl;
  for (unsigned i = 0; i <= 5; ++i) {
    std::cout << "is_even(" << i << ") = " << is_even(i) << std::endl;
  }
  return 0;
}
