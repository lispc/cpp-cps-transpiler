[Detected recursive function] with_local

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: with_local ===

int with_local(int n) {
  int acc = 1;
  while (!(n <= 1)) {
    auto m = n - 1;

    acc = acc * n;
    auto new_n = m;
    n = new_n;
  }
  return acc;
}


