[Detected recursive function] gcd

// ================================
// Generated iterative code
// ================================

// === Generated tail-recursion optimized code for function: gcd ===

int gcd(int a, int b) {
  while (1) {
    if (b == 0) return a;
    auto new_a = b;
    auto new_b = a % b;
    a = new_a;
    b = new_b;
  }
}


