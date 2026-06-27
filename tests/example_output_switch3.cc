[Detected recursive function] fact_switch

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: fact_switch ===

int fact_switch(int n) {
  int product = 1;
  while (!((n == 0)) && !((n == 1))) {
    product = product * n;
    auto next_n = n - 1;
    n = next_n;
  }
  return product;
}


