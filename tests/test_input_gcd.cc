// 测试输入：多参数尾递归 GCD
// 目标：验证 TRO 路径支持多参数

int gcd(int a, int b) {
  if (b == 0) return a;
  return gcd(b, a % b);
}
