[Detected recursive function] with_local

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: with_local ===


int with_local(int n) {
  int product = 1;
  while (!(n <= 1)) {
    auto m = n - 1;
    product = product * n;
    auto next_n = m;
    n = next_n;
  }
  return product;
}


