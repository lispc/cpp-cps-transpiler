// 测试输入：switch 作为 base case
// 目标：验证转化器支持 switch 语句映射为 base case 链

int fib_switch(int n) {
  switch (n) {
    case 0:
    case 1: return n;
    default: return fib_switch(n - 1) + fib_switch(n - 2);
  }
}
