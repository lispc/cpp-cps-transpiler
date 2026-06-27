[Detected recursive function] fib_switch

// ================================
// Generated iterative code
// ================================

// === Generated binary-stack code for function: fib_switch ===

#include <vector>

struct fib_switchFrame {
  int n;
  fib_switchFrame(int n) : n(n) {}
};

int fib_switch(int n) {
  std::vector<fib_switchFrame> stack;
  stack.emplace_back(n);
  int result = 0;
  while (!stack.empty()) {
    auto cur = stack.back();
    stack.pop_back();
    if ((cur.n == 0)) {
      result += cur.n;
    }
    else if ((cur.n == 1)) {
      result += cur.n;
    }
    else {
      stack.emplace_back(cur.n - 2);
      stack.emplace_back(cur.n - 1);
    }
  }
  return result;
}


