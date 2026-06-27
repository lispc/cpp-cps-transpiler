// 测试输入：guard early return

int fact_guard(int n) {
  if (n < 0) return -1;
  if (n <= 1) return 1;
  return n * fact_guard(n - 1);
}
