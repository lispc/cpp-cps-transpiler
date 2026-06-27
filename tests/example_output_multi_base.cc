[Detected recursive function] multi_base

// ================================
// Generated iterative code
// ================================

// === Generated tupling code for function: multi_base ===

#include <array>

int multi_base(int n) {
  if (n <= 1) {
    if (n <= 0) return 0;
    else if (n == 1) return 1;
    return 0;
  }
  std::array<int, 2> vals;
  {
    int n = 0;
    if (n <= 0)
      vals[0] = 0;
    else if (n == 1)
      vals[0] = 1;
    else vals[0] = 0;
  }
  {
    int n = 1;
    if (n <= 0)
      vals[1] = 0;
    else if (n == 1)
      vals[1] = 1;
    else vals[1] = 0;
  }
  for (int i = 2; i <= n; ++i) {
    int next = (vals[1] + vals[0]);
    for (int j = 0; j < 1; ++j)
      vals[j] = vals[j + 1];
    vals[1] = next;
  }
  return vals[1];
}


