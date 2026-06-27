// 测试输入：相互递归（尾调用形式）
// 目标：验证转化器支持 odd/even 相互递归

int even(int n);

int odd(int n) {
  if (n == 0) return 0;
  return even(n - 1);
}

int even(int n) {
  if (n == 0) return 1;
  return odd(n - 1);
}
