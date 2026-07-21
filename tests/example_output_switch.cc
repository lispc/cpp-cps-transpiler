[Detected recursive function] fib_switch

// ================================
// Generated iterative code
// ================================

#include <vector>

// === Generated binary-stack code for function: fib_switch ===

struct __cps_fib_switchFrame {
  int n;
  __cps_fib_switchFrame(int n_) : n(n_) {}
};

int fib_switch(int n) {
  std::vector<__cps_fib_switchFrame> __cps_stack;
  __cps_stack.emplace_back(__cps_fib_switchFrame(n));
  int result = 0;
  while (!__cps_stack.empty()) {
    auto __cps_cur = __cps_stack.back();
    __cps_stack.pop_back();
    auto n = __cps_cur.n;
    if ((__cps_cur.n == 0)) {
      result += __cps_cur.n;
    } else if ((__cps_cur.n == 1)) {
      result += __cps_cur.n;
    } else {
      __cps_stack.emplace_back(__cps_fib_switchFrame((__cps_cur.n - 2)));
      __cps_stack.emplace_back(__cps_fib_switchFrame((__cps_cur.n - 1)));
    }
  }
  return result;
}


