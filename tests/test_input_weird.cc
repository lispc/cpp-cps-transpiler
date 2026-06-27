// 测试输入：非标准二元递归（带系数）
// 目标：验证通用显式栈规则能处理多洞非平凡表达式

int weird(int n) {
  if (n <= 1) return n;
  return weird(n - 1) + 2 * weird(n - 2);
}
