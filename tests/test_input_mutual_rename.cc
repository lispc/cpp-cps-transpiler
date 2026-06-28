// 测试输入：相互递归且参数名不一致
// 目标：验证转化器能归一化组成员的参数名

int even(int m);

int odd(int n) {
  if (n == 0) return 0;
  return even(n - 1);
}

int even(int m) {
  if (m == 0) return 1;
  return odd(m - 1);
}
