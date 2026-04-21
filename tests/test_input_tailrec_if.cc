// 测试输入：嵌套 if 中的尾递归
// 目标：验证通用尾递归检测能识别 if/else 各 branch 中的尾递归

int tailrec_if(int n) {
  if (n <= 0) return 0;
  if (n <= 10) return tailrec_if(n);
  return tailrec_if(n - 1);
}
