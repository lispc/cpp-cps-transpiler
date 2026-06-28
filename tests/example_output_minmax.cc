Error while trying to load a compilation database:
Could not auto-detect compilation database for file "tests/test_input_minmax.cc"
No compilation database found in /Users/zhangzhuo/repos/personal/cps/tests or any parent directory
fixed-compilation-database: Error while opening fixed database: No such file or directory
json-compilation-database: Error while opening JSON database: No such file or directory
Running without flags.
[Detected recursive function] min_tree

// ================================
// Generated iterative code
// ================================

// === Generated generic-stack code for function: min_tree ===

#include <vector>
#include <algorithm>

struct __cps_min_treeFrame {
  int n;
  __cps_min_treeFrame(int n_) : n(n_) {}
};

struct __cps_min_treeEntry {
  enum class Tag { Frame, Marker } tag;
  __cps_min_treeFrame frame;
  int count;
  __cps_min_treeEntry(__cps_min_treeFrame f) : tag(Tag::Frame), frame(std::move(f)), count(0) {}
  __cps_min_treeEntry(int c, __cps_min_treeFrame f) : tag(Tag::Marker), frame(std::move(f)), count(c) {}
};

int min_tree(int n) {
  std::vector<__cps_min_treeEntry> __cps_stack;
  std::vector<int> __cps_values;
  __cps_stack.emplace_back(__cps_min_treeFrame(n));
  while (!__cps_stack.empty()) {
    auto __cps_entry = __cps_stack.back();
    __cps_stack.pop_back();
    if (__cps_entry.tag == __cps_min_treeEntry::Tag::Marker) {
      auto __cps_cur = __cps_entry.frame;
      auto n = __cps_cur.n;
      int v0 = __cps_values.back();
      __cps_values.pop_back();
      int v1 = __cps_values.back();
      __cps_values.pop_back();
      __cps_values.push_back(std::min(v0, v1));
    }
    else {
      auto __cps_cur = __cps_entry.frame;
      auto n = __cps_cur.n;
      if (n <= 0)
        __cps_values.push_back(10);
      else if (n == 1)
        __cps_values.push_back(1);
      else {
        __cps_stack.emplace_back(__cps_min_treeEntry(2, __cps_min_treeFrame(n)));
        __cps_stack.emplace_back(__cps_min_treeFrame(n - 1));
        __cps_stack.emplace_back(__cps_min_treeFrame(n - 2));
      }
    }
  }
  return __cps_values.back();
}


