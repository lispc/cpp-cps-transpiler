// 测试输入：三元运算符作为 base case

int fact_t(int n) {
  return n <= 1 ? 1 : n * fact_t(n - 1);
}
