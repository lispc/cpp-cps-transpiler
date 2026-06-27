[Detected recursive function] impure_base

// ================================
// Generated iterative code
// ================================

// === Generated generic-stack code for function: impure_base ===

#include <vector>
#include <variant>

struct impure_baseFrame {
  int n;
  impure_baseFrame(int n) : n(n) {}
};

struct impure_baseCombineMarker {
  int count;
  impure_baseFrame frame;
  impure_baseCombineMarker(int c, impure_baseFrame f) : count(c), frame(std::move(f)) {}
};

int impure_base(int n) {
  std::vector<std::variant<impure_baseFrame, impure_baseCombineMarker>> stack;
  stack.emplace_back(impure_baseFrame(n));
  std::vector<int> values;
  while (!stack.empty()) {
    auto entry = stack.back();
    stack.pop_back();
    if (std::holds_alternative<impure_baseCombineMarker>(entry)) {
      auto marker = std::get<impure_baseCombineMarker>(entry);
      auto cur = marker.frame;
      auto n = cur.n;
      int v0 = values.back();
      values.pop_back();
      values.push_back((v0 + n));
    }
    else {
      auto cur = std::get<impure_baseFrame>(entry);
      auto n = cur.n;
      if (n <= 1)
        values.push_back(++counter);
      else {
        stack.emplace_back(impure_baseCombineMarker(1, cur));
        stack.emplace_back(impure_baseFrame(n - 1));
      }
    }
  }
  return values.back();
}


