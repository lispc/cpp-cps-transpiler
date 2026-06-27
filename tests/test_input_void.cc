// 测试输入：void 尾递归

extern void emit(int x);

void print_down(int n) {
  if (n <= 0) return;
  emit(n);
  print_down(n - 1);
}
