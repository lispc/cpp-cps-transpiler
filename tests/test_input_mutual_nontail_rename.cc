// 测试输入：非尾调用相互递归且参数名不一致
// f(n) = 1 + g(n-1), g(n) = f(n-1)

int f(int a);
int g(int b) {
  if (b == 0) return 0;
  return f(b - 1);
}
int f(int a) {
  if (a == 0) return 1;
  return 1 + g(a - 1);
}
