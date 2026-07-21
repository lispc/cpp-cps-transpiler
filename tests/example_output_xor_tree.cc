[Detected recursive function] xor_tree

// ================================
// Generated iterative code
// ================================

#include <vector>

// === Generated memoized code for function: xor_tree ===

int xor_tree(int n) {
  if (n <= 1) {
    if (n <= 0) {
      return 0;
    } else if (n == 1) {
      return 1;
    }
    return 0;
  }
  std::vector<int> dp(n + 1);
  dp[0] = 0;
  dp[1] = 1;
  for (int i = 2; i <= n; ++i) {
    dp[i] = dp[i - 1] ^ dp[i - 2];
  }
  return dp[n];
}


