// 测试输入：普通递归版 fibonacci
// 目标：被 transpiler 自动转换成 CPS + Trampoline 迭代版本

int fib(int n) {
  if (n <= 1) return n;
  return fib(n - 1) + fib(n - 2);
}

int main() {
  int x = fib(10);
  return x;
}
