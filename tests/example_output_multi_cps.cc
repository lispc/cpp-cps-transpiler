Error while trying to load a compilation database:
Could not auto-detect compilation database for file "tests/test_input_multi_cps.cc"
No compilation database found in /Users/zhangzhuo/repos/personal/cps/tests or any parent directory
fixed-compilation-database: Error while opening fixed database: No such file or directory
json-compilation-database: Error while opening JSON database: No such file or directory
Running without flags.
[Detected recursive function] multi_cps

// ================================
// Generated iterative code
// ================================

// === Generated binary-stack code for function: multi_cps ===

#include <vector>

struct __cps_multi_cpsFrame {
  int a;
  int b;
  __cps_multi_cpsFrame(int a_, int b_) : a(a_), b(b_) {}
};

int multi_cps(int a, int b) {
  std::vector<__cps_multi_cpsFrame> __cps_stack;
  __cps_stack.emplace_back(__cps_multi_cpsFrame(a, b));
  int result = 0;
  while (!__cps_stack.empty()) {
    auto __cps_cur = __cps_stack.back();
    __cps_stack.pop_back();
    auto a = __cps_cur.a;
    auto b = __cps_cur.b;
    if ((__cps_cur.a <= 0)) {
      result += __cps_cur.b;
    }
    else {
      __cps_stack.emplace_back(__cps_multi_cpsFrame((__cps_cur.a - 2), __cps_cur.b));
      __cps_stack.emplace_back(__cps_multi_cpsFrame((__cps_cur.a - 1), __cps_cur.b));
    }
  }
  return result;
}


