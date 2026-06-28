// 测试输入：GenericStackRule 捕获非 int 局部变量（聚合类）
// 目标：验证 captured local 的默认初始化按类型生成，而不是硬编码 0

struct Box { int v; };

int sum_boxed(int n) {
  if (n <= 0) return 0;
  Box b{n};
  return sum_boxed(n - 2) - b.v + sum_boxed(n - 1);
}
