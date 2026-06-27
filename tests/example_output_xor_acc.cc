[Detected recursive function] xor_acc

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: xor_acc ===

int xor_acc(int n) {
  int acc = 0;
  while (!(n <= 0)) {
    acc = acc ^ n;
    auto new_n = n - 1;
    n = new_n;
  }
  return acc;
}


