// 测试输入：非递归函数调用包裹递归调用
// 目标：验证通用表达式 CPS 能处理 CallExpr 上下文中的递归调用

int double_it(int x) { return x * 2; }

int double_fact(int n) {
  if (n <= 1) return 1;
  return double_it(double_fact(n - 1));
}
