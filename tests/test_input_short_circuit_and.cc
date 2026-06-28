// 测试输入：&& 短路递归表达式
// 目标：验证 BinaryStackRule 不再处理 &&，改由 GenericStackRule 处理

bool all_small(int n) {
  if (n <= 0) return true;
  return all_small(n - 1) && (n < 4);
}
