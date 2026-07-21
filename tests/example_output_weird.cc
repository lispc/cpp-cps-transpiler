[Detected recursive function] weird

// ================================
// Generated iterative code
// ================================

#include <vector>

// === Generated memoized code for function: weird ===

int weird(int n) {
  if (n <= 1) {
    if (n <= 1) {
      return n;
    }
    return 0;
  }
  std::vector<int> dp(n + 1);
  dp[0] = 0;
  dp[1] = 1;
  for (int i = 2; i <= n; ++i) {
    dp[i] = dp[i - 1] + (2 * dp[i - 2]);
  }
  return dp[n];
}


