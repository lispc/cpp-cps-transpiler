// 测试输入：带重叠子问题的非齐次线性递推
// 适合 MemoizationRule：dp[i] = dp[i-1] + 2*dp[i-2] + 1

int memo_weird(int n) {
  if (n <= 1) return n;
  return memo_weird(n - 1) + 2 * memo_weird(n - 2) + 1;
}
