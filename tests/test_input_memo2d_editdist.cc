int min(int x, int y) { return x < y ? x : y; }

int editdist(const char *a, const char *b, int i, int j) {
  if (i == 0) return j;
  if (j == 0) return i;
  return min(min(editdist(a, b, i - 1, j) + 1, editdist(a, b, i, j - 1) + 1),
             editdist(a, b, i - 1, j - 1) + (a[i - 1] == b[j - 1] ? 0 : 1));
}
