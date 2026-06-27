[Detected recursive function] weird

// ================================
// Generated iterative code
// ================================

// === Generated generic-stack code for function: weird ===

#include <vector>
#include <variant>

struct weirdFrame {
  int n;
  weirdFrame(int n) : n(n) {}
};

int weird(int n) {
  std::vector<std::variant<weirdFrame, int>> stack;
  stack.emplace_back(weirdFrame(n));
  std::vector<int> values;
  while (!stack.empty()) {
    auto entry = stack.back();
    stack.pop_back();
    if (std::holds_alternative<int>(entry)) {
      int v0 = values.back();
      values.pop_back();
      int v1 = values.back();
      values.pop_back();
      values.push_back((v0 + (2 * v1)));
    }
    else {
      auto cur = std::get<weirdFrame>(entry);
      auto n = cur.n;
      if (n <= 1)
        values.push_back(n);
      else {
        stack.emplace_back(2);
        stack.emplace_back(weirdFrame(n - 1));
        stack.emplace_back(weirdFrame(n - 2));
      }
    }
  }
  return values.back();
}


