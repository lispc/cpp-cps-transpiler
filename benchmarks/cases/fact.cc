long long fact(int n) {
  if (n <= 1) return 1;
  return (n * fact(n - 1)) % 1000000007LL;
}
