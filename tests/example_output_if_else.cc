[Detected recursive function] if_else

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: if_else ===

int if_else(int n) {
  int acc = 1;
  while (!(n <= 1)) {
    acc = acc * 2;
    auto new_n = n - 1;
    n = new_n;
  }
  return acc;
}


