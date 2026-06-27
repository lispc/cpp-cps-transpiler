// 测试输入：min / max 二元递归
// 目标：验证二元栈模式支持 min/max 结果再加工

int min(int a, int b) { return a < b ? a : b; }

int min_tree(int n) {
  if (n <= 0) return 10;
  if (n == 1) return 1;
  return min(min_tree(n - 1), min_tree(n - 2));
}
