// 测试输入：XOR 二元递归
// 目标：验证二元栈模式支持 ^ 运算符

int xor_tree(int n) {
  if (n <= 0) return 0;
  if (n == 1) return 1;
  return xor_tree(n - 1) ^ xor_tree(n - 2);
}
