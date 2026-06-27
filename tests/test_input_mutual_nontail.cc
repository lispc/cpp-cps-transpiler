// 测试输入：非尾调用相互递归
// f(n) = 1 + g(n-1), g(n) = f(n-1)

int f(int n);
int g(int n) {
  if (n == 0) return 0;
  return f(n - 1);
}
int f(int n) {
  if (n == 0) return 1;
  return 1 + g(n - 1);
}
