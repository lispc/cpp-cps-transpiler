[Detected recursive function] acc_multi_base

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: acc_multi_base ===

int acc_multi_base(int n) {
  int sum = 0;
  while (!(n <= 0) && !(n == 1)) {
    sum = sum + n;
    auto next_n = n - 1;
    n = next_n;
  }
  return sum;
}


