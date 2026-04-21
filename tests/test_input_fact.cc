// 测试输入：递归阶乘
// 目标：验证单边乘法递归的 CPS 转换

int fact(int n) {
  if (n <= 1) return 1;
  return n * fact(n - 1);
}
