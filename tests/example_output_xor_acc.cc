[Detected recursive function] xor_acc

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: xor_acc ===


int xor_acc(int n) {
  int xors = 0;
  while (!(n <= 0)) {
    xors = xors ^ n;
    auto next_n = n - 1;
    n = next_n;
  }
  return xors;
}


