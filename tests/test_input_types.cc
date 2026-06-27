// 测试输入：非 int 返回类型

long long fib_ll(long long n) {
  if (n <= 1) return n;
  return fib_ll(n - 1) + fib_ll(n - 2);
}

unsigned fact_unsigned(unsigned n) {
  if (n <= 1) return 1;
  return n * fact_unsigned(n - 1);
}

bool is_even(unsigned n) {
  if (n == 0) return true;
  return !is_even(n - 1);
}
