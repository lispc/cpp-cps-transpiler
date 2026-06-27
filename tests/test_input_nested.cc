// 测试输入：嵌套递归
// 目标：验证 DefunctionalizedRule 能处理递归调用的参数仍是递归调用

int nested_fact(int n) {
  if (n <= 1) return 1;
  return nested_fact(nested_fact(n - 1));
}
