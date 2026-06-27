// 测试输入：多个 base case
// 目标：验证转化器支持多个 if-return 分支

int multi_base(int n) {
  if (n <= 0) return 0;
  if (n == 1) return 1;
  return multi_base(n - 1) + multi_base(n - 2);
}
