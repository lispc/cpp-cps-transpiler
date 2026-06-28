#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "memo_weird(" << i << ") = " << memo_weird(i) << std::endl;
  }
  return 0;
}
