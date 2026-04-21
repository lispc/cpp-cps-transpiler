// 测试输入：纯尾递归函数
// 目标：验证 transpiler 对纯尾递归的 while 循环优化

int clamp_down(int n) {
  if (n <= 10) return n;
  return clamp_down(n - 1);
}
