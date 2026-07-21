struct IntVec {
  int data[32];
  int size;
  IntVec() : size(0) {}
  void push_back(int v) {
    data[size] = v;
    size = size + 1;
  }
};

IntVec down(int n) {
  if (n <= 0) return IntVec();
  IntVec r = down(n - 1);
  r.push_back(n);
  return r;
}
