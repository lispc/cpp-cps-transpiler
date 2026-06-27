[Detected recursive function] gcd

// ================================
// Generated iterative code
// ================================

// === Generated tail-recursion optimized code for function: gcd ===

int gcd(int a, int b) {
  while (1) {
    if (b == 0) return a;
    auto next_a = b;
    auto next_b = a % b;
    a = next_a;
    b = next_b;
  }
}


