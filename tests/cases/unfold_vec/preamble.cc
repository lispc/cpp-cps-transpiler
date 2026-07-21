struct IntVec {
  int data[32];
  int size;
  IntVec() : size(0) {}
  void push_back(int v) {
    data[size] = v;
    size = size + 1;
  }
};
