int max(int x, int y) { return x > y ? x : y; }

int lcs(const char *a, const char *b, int i, int j) {
  if (i == 0 || j == 0) return 0;
  return a[i - 1] == b[j - 1] ? lcs(a, b, i - 1, j - 1) + 1
                              : max(lcs(a, b, i - 1, j), lcs(a, b, i, j - 1));
}
