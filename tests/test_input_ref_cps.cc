// 测试输入：引用参数 + 非尾递归
// 目标：验证显式栈规则支持引用参数（按值捕获）

int ref_sum(int& a, int n) {
  if (n <= 0) return 0;
  return ref_sum(a, n - 1) + a;
}
