#include <iostream>
int main() {
  for (int i = 0; i <= 3; ++i)
    for (int j = 0; j <= 3; ++j)
      std::cout << "grid(" << i << ", " << j << ") = " << grid(i, j) << std::endl;
  return 0;
}
