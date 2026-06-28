// 测试输入：DefunctionalizedRule 返回非 int 类型（double）
// 目标：验证默认返回值与默认参数按类型生成

double half(double x) { return x * 0.5; }

double weird_double(int n) {
  if (n <= 0) return 0.25;
  return half(weird_double(weird_double(n - 1)));
}
