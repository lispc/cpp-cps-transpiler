// Showcase 2：嵌套递归 + 多路相互递归
//
// 1. McCarthy 91：著名的嵌套递归，n <= 100 时结果恒为 91
// 2. mod3 系统：三路相互递归，判断 n 是否能被 3 整除

int mc91(int n) {
  if (n > 100) return n - 10;
  return mc91(mc91(n + 11));
}

int mod0(int n);
int mod1(int n);
int mod2(int n);

int mod0(int n) {
  if (n == 0) return 1;
  return mod2(n - 1);
}

int mod1(int n) {
  if (n == 0) return 0;
  return mod0(n - 1);
}

int mod2(int n) {
  if (n == 0) return 0;
  return mod1(n - 1);
}
