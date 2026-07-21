int grid(int i, int j) {
  if (i == 0 || j == 0) return 1;
  return grid(i - 1, j) + grid(i, j - 1);
}
