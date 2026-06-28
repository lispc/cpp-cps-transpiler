Error while trying to load a compilation database:
Could not auto-detect compilation database for file "tests/test_input_impure_base.cc"
No compilation database found in /Users/zhangzhuo/repos/personal/cps/tests or any parent directory
fixed-compilation-database: Error while opening fixed database: No such file or directory
json-compilation-database: Error while opening JSON database: No such file or directory
Running without flags.
[Detected recursive function] impure_base

// ================================
// Generated iterative code
// ================================

// === Generated generic-stack code for function: impure_base ===

#include <vector>

struct __cps_impure_baseFrame {
  int n;
  __cps_impure_baseFrame(int n_) : n(n_) {}
};

struct __cps_impure_baseEntry {
  enum class Tag { Frame, Marker } tag;
  __cps_impure_baseFrame frame;
  int count;
  __cps_impure_baseEntry(__cps_impure_baseFrame f) : tag(Tag::Frame), frame(std::move(f)), count(0) {}
  __cps_impure_baseEntry(int c, __cps_impure_baseFrame f) : tag(Tag::Marker), frame(std::move(f)), count(c) {}
};

int impure_base(int n) {
  std::vector<__cps_impure_baseEntry> __cps_stack;
  std::vector<int> __cps_values;
  __cps_stack.emplace_back(__cps_impure_baseFrame(n));
  while (!__cps_stack.empty()) {
    auto __cps_entry = __cps_stack.back();
    __cps_stack.pop_back();
    if (__cps_entry.tag == __cps_impure_baseEntry::Tag::Marker) {
      auto __cps_cur = __cps_entry.frame;
      auto n = __cps_cur.n;
      int v0 = __cps_values.back();
      __cps_values.pop_back();
      __cps_values.push_back(v0 + n);
    }
    else {
      auto __cps_cur = __cps_entry.frame;
      auto n = __cps_cur.n;
      if (n <= 1)
        __cps_values.push_back(++counter);
      else {
        __cps_stack.emplace_back(__cps_impure_baseEntry(1, __cps_impure_baseFrame(n)));
        __cps_stack.emplace_back(__cps_impure_baseFrame(n - 1));
      }
    }
  }
  return __cps_values.back();
}


