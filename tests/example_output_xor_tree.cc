Error while trying to load a compilation database:
Could not auto-detect compilation database for file "tests/test_input_xor_tree.cc"
No compilation database found in /Users/zhangzhuo/repos/personal/cps/tests or any parent directory
fixed-compilation-database: Error while opening fixed database: No such file or directory
json-compilation-database: Error while opening JSON database: No such file or directory
Running without flags.
[Detected recursive function] xor_tree

// ================================
// Generated iterative code
// ================================

// === Generated memoized code for function: xor_tree ===

#include <vector>

int xor_tree(int n) {
  if (n <= 1) {
    if (n <= 0) return 0;
    else if (n == 1) return 1;
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


