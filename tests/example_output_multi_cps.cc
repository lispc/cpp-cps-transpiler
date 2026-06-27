[Detected recursive function] multi_cps

// ================================
// Generated iterative code
// ================================

// === Generated binary-stack code for function: multi_cps ===

#include <vector>

struct multi_cpsFrame {
  int a;
  int b;
  multi_cpsFrame(int a, int b) : a(a), b(b) {}
};

int multi_cps(int a, int b) {
  std::vector<multi_cpsFrame> stack;
  stack.emplace_back(a, b);
  int result = 0;
  while (!stack.empty()) {
    auto cur = stack.back();
    stack.pop_back();
    if (cur.a <= 0) {
      result += cur.b;
    }
    else {
      stack.emplace_back(cur.a - 2, cur.b);
      stack.emplace_back(cur.a - 1, cur.b);
    }
  }
  return result;
}


