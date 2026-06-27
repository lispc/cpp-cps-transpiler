// 测试输入：base case 含有副作用
// 目标：验证 AccumulatorRule 因纯度检查失败而降级到 GenericStackRule

int counter = 0;

int impure_base(int n) {
  if (n <= 1) return ++counter;
  return impure_base(n - 1) + n;
}
