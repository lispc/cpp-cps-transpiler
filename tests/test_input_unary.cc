// 测试输入：一元运算符包裹递归调用
// 目标：验证通用显式栈规则能处理 UnaryOperator 上下文

int neg_fact(int n) {
  if (n <= 1) return -1;
  return -(neg_fact(n - 1));
}
