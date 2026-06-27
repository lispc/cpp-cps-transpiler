// 测试输入：多参数双边递归
// 目标：验证显式栈规则支持多参数

int multi_cps(int a, int b) {
  if (a <= 0) return b;
  return multi_cps(a - 1, b) + multi_cps(a - 2, b);
}
