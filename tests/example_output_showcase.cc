[Detected recursive function] binomial

// ================================
// Generated iterative code
// ================================

// === Generated binary-stack code for function: binomial ===

#include <vector>

struct binomialFrame {
  int n;
  int k;
  binomialFrame(int n, int k) : n(n), k(k) {}
};

int binomial(int n, int k) {
  std::vector<binomialFrame> stack;
  stack.emplace_back(n, k);
  int result = 0;
  while (!stack.empty()) {
    auto cur = stack.back();
    stack.pop_back();
    if (cur.k == 0 || cur.k == cur.n) {
      result += 1;
    }
    else {
      stack.emplace_back(cur.n - 1, cur.k);
      stack.emplace_back(cur.n - 1, cur.k - 1);
    }
  }
  return result;
}


