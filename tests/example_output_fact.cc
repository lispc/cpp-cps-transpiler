[Detected recursive function] fact

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: fact ===


int fact(int n) {
  int product = 1;
  while (!(n <= 1)) {
    product = product * n;
    auto next_n = n - 1;
    n = next_n;
  }
  return product;
}


