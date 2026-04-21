
// ================================
// Generated CPS + Trampoline code
// ================================

// === Generated tail-recursion optimized code for function: clamp_down ===

int clamp_down(int n) {
  while (1) {
    if (n <= 10) return n;
    auto new_n = n - 1;
    n = new_n;
  }
}


