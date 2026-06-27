[Detected recursive function] xor_tree

// ================================
// Generated iterative code
// ================================

// === Generated binary-stack code for function: xor_tree ===

#include <vector>

struct xor_treeFrame {
  int n;
  xor_treeFrame(int n) : n(n) {}
};

int xor_tree(int n) {
  std::vector<xor_treeFrame> stack;
  stack.emplace_back(n);
  int result = 0;
  while (!stack.empty()) {
    auto cur = stack.back();
    stack.pop_back();
    if (cur.n <= 0) {
      result ^= 0;
    }
    else if (cur.n == 1) {
      result ^= 1;
    }
    else {
      stack.emplace_back(cur.n - 2);
      stack.emplace_back(cur.n - 1);
    }
  }
  return result;
}


