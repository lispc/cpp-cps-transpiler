// 测试输入：XOR 累加器
// 目标：验证累加器模式支持 ^ 运算符

int xor_acc(int n) {
  if (n <= 0) return 0;
  return xor_acc(n - 1) ^ n;
}
