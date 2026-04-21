// === 自动生成的尾递归优化代码（来自 cps-transpiler）===
// 输入: tests/test_input_tailrec.cc

int clamp_down(int n) {
  while (1) {
    if (n <= 10) return n;
    auto new_n = n - 1;
    n = new_n;
  }
}

// === 测试 main ===
#include <iostream>
#include <cassert>

int main() {
  assert(clamp_down(5) == 5);
  assert(clamp_down(10) == 10);
  assert(clamp_down(15) == 10);
  assert(clamp_down(100) == 10);
  assert(clamp_down(0) == 0);
  assert(clamp_down(-5) == -5);

  std::cout << "clamp_down(5) = " << clamp_down(5) << std::endl;
  std::cout << "clamp_down(15) = " << clamp_down(15) << std::endl;
  std::cout << "clamp_down(100) = " << clamp_down(100) << std::endl;
  std::cout << "All tail-recursion tests passed!" << std::endl;
  return 0;
}
