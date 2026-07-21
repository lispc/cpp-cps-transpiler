#include <iostream>
static void show(int n) {
  IntVec v = down(n);
  std::cout << "down(" << n << ") = [";
  for (int i = 0; i < v.size; ++i) {
    if (i) std::cout << ", ";
    std::cout << v.data[i];
  }
  std::cout << "]" << std::endl;
}
int main() {
  show(0);
  show(1);
  show(5);
  return 0;
}
