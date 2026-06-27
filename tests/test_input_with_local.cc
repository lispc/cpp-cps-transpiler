// 测试输入：递归调用前存在局部变量声明
// 目标：验证转化器支持 leading statements

int with_local(int n) {
  if (n <= 1) return 1;
  auto m = n - 1;
  return with_local(m) * n;
}
