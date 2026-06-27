[Detected recursive function] clamp_down

// ================================
// Generated iterative code
// ================================

// === Generated tail-recursion optimized code for function: clamp_down ===

int clamp_down(int n) {
  while (1) {
    if (n <= 10) return n;
    auto next_n = n - 1;
    n = next_n;
  }
}


