#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "fact_switch(" << i << ") = " << fact_switch(i) << std::endl;
  }
  return 0;
}
