// 测试输入：引用参数 + 尾递归（带副作用）
// 目标：验证 TRO 路径支持引用参数并保留副作用语义

int ref_accumulate(int& a, int b) {
  if (b <= 0) return a;
  return ref_accumulate(++a, b - 1);
}
