[Detected recursive function] min_tree

// ================================
// Generated iterative code
// ================================

// === Generated generic-stack code for function: min_tree ===

#include <vector>
#include <variant>
#include <algorithm>

struct min_treeFrame {
  int n;
  min_treeFrame(int n) : n(n) {}
};

struct min_treeCombineMarker {
  int count;
  min_treeCombineMarker(int c) : count(c) {}
};

int min_tree(int n) {
  std::vector<std::variant<min_treeFrame, min_treeCombineMarker>> stack;
  stack.emplace_back(min_treeFrame(n));
  std::vector<int> values;
  while (!stack.empty()) {
    auto entry = stack.back();
    stack.pop_back();
    if (std::holds_alternative<min_treeCombineMarker>(entry)) {
      int v0 = values.back();
      values.pop_back();
      int v1 = values.back();
      values.pop_back();
      values.push_back(std::min(v0, v1));
    }
    else {
      auto cur = std::get<min_treeFrame>(entry);
      auto n = cur.n;
      if (n <= 0)
        values.push_back(10);
      else if (n == 1)
        values.push_back(1);
      else {
        stack.emplace_back(min_treeCombineMarker(2));
        stack.emplace_back(min_treeFrame(n - 1));
        stack.emplace_back(min_treeFrame(n - 2));
      }
    }
  }
  return values.back();
}


