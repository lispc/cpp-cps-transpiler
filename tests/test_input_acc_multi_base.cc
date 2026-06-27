// 测试输入：多 base case 的累加器
// 目标：验证 AccumulatorRule 支持多个相同返回值的 base case

int acc_multi_base(int n) {
  if (n <= 0) return 0;
  if (n == 1) return 0;
  return acc_multi_base(n - 1) + n;
}
