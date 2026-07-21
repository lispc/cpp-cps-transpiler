[Detected recursive function] if_else

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: if_else ===


int if_else(int n) {
  int product = 1;
  while (!(n <= 1)) {
    product = product * 2;
    auto next_n = n - 1;
    n = next_n;
  }
  return product;
}


