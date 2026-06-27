// Showcase: 二项式系数 C(n, k) 的多参数双边递归
// 目标：演示转化器能把经典组合数学递归转换成可读的多参数显式栈迭代版本

int binomial(int n, int k) {
  if (k == 0 || k == n) return 1;
  return binomial(n - 1, k - 1) + binomial(n - 1, k);
}
