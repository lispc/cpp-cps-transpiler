// 测试输入：减法 accumulator

int sub_acc(int n) {
  if (n <= 1) return 1;
  return sub_acc(n - 1) - n;
}
