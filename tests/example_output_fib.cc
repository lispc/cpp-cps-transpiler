[Detected recursive function] fib

// ================================
// Generated iterative code
// ================================

// === Generated tupling code for function: fib ===

#include <array>

int fib(int n) {
  if (n <= 1) {
    if (n <= 1) return n;
    return 0;
  }
  std::array<int, 2> vals;
  vals[0] = 0;
  vals[1] = 1;
  for (int i = 2; i <= n; ++i) {
    int next = (vals[1] + vals[0]);
    vals[0] = vals[1];
    vals[1] = next;
  }
  return vals[1];
}


