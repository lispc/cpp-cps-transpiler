// 测试输入：指针参数 + 非尾递归
// 目标：验证显式栈规则支持指针参数

int sum_ptr(int* arr, int n) {
  if (n <= 0) return 0;
  return arr[n - 1] + sum_ptr(arr, n - 1);
}
