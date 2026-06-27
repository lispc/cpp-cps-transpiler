// 测试输入：if-else 结构
// 目标：验证转化器支持 if-else return

int if_else(int n) {
  if (n <= 1) return 1;
  else return if_else(n - 1) * 2;
}
