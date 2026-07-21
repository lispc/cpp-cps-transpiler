struct Str {
  char buf[128];
  int len;
  Str() : len(0) { buf[0] = 0; }
  void append(char c) {
    buf[len] = c;
    len = len + 1;
    buf[len] = 0;
  }
};
