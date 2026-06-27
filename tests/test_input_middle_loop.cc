// 测试输入：middle statement 含 for 循环 + 局部变量被 RecExpr 引用

int sum_loop(int n) {
  if (n <= 1) return n;
  int s = 0;
  for (int i = 0; i < n; ++i) s += i;
  return sum_loop(n - 1) + s;
}
