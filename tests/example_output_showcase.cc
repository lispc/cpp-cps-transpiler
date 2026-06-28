Error while trying to load a compilation database:
Could not auto-detect compilation database for file "tests/test_input_showcase.cc"
No compilation database found in /Users/zhangzhuo/repos/personal/cps/tests or any parent directory
fixed-compilation-database: Error while opening fixed database: No such file or directory
json-compilation-database: Error while opening JSON database: No such file or directory
Running without flags.
[Detected recursive function] binomial

// ================================
// Generated iterative code
// ================================

// === Generated binary-stack code for function: binomial ===

#include <vector>

struct __cps_binomialFrame {
  int n;
  int k;
  __cps_binomialFrame(int n_, int k_) : n(n_), k(k_) {}
};

int binomial(int n, int k) {
  std::vector<__cps_binomialFrame> __cps_stack;
  __cps_stack.emplace_back(__cps_binomialFrame(n, k));
  int result = 0;
  while (!__cps_stack.empty()) {
    auto __cps_cur = __cps_stack.back();
    __cps_stack.pop_back();
    auto n = __cps_cur.n;
    auto k = __cps_cur.k;
    if (((__cps_cur.k == 0) || (__cps_cur.k == __cps_cur.n))) {
      result += 1;
    }
    else {
      __cps_stack.emplace_back(__cps_binomialFrame((__cps_cur.n - 1), __cps_cur.k));
      __cps_stack.emplace_back(__cps_binomialFrame((__cps_cur.n - 1), (__cps_cur.k - 1)));
    }
  }
  return result;
}


