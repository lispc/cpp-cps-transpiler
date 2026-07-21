[Detected recursive function] impure_base

// ================================
// Generated iterative code
// ================================

#include <vector>

// === Generated generic-stack code for function: impure_base ===

struct __cps_impure_baseFrame {
  int n;
  __cps_impure_baseFrame(int n_) : n(n_) {}
};

enum class __cps_impure_baseEntryTag {
  Frame,
  Marker
};

struct __cps_impure_baseEntry {
  __cps_impure_baseEntryTag tag;
  __cps_impure_baseFrame frame;
  int count;
  __cps_impure_baseEntry(__cps_impure_baseFrame f) : tag(__cps_impure_baseEntryTag::Frame), frame(std::move(f)), count(0) {}
  __cps_impure_baseEntry(int c, __cps_impure_baseFrame f) : tag(__cps_impure_baseEntryTag::Marker), frame(std::move(f)), count(c) {}
};

int impure_base(int n) {
  std::vector<__cps_impure_baseEntry> __cps_stack;
  std::vector<int> __cps_values;
  __cps_stack.emplace_back(__cps_impure_baseFrame(n));
  while (!__cps_stack.empty()) {
    auto __cps_entry = __cps_stack.back();
    __cps_stack.pop_back();
    if (__cps_entry.tag == __cps_impure_baseEntryTag::Marker) {
      auto __cps_cur = __cps_entry.frame;
      auto n = __cps_cur.n;
      int v0 = __cps_values.back();
      __cps_values.pop_back();
      __cps_values.push_back(v0 + n);
    } else {
      auto __cps_cur = __cps_entry.frame;
      auto n = __cps_cur.n;
      if (n <= 1) {
        __cps_values.push_back(++counter);
      } else {
        __cps_stack.emplace_back(__cps_impure_baseEntry(1, __cps_impure_baseFrame(n)));
        __cps_stack.emplace_back(__cps_impure_baseFrame(n - 1));
      }
    }
  }
  return __cps_values.back();
}


