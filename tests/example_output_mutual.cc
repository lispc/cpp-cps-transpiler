[Detected recursive function] odd
[Detected recursive function] even

// ================================
// Generated iterative code
// ================================

// === Generated mutual-recursion code ===

enum class oddMutualTag {
  odd,
  even,
};

int odd_dispatch(oddMutualTag tag, int n) {
  while (1) {
    switch (tag) {
      case oddMutualTag::odd: {
        if (n == 0) return 0;
        tag = oddMutualTag::even;
        auto next_n = n - 1;
        n = next_n;
        break;
      }
      case oddMutualTag::even: {
        if (n == 0) return 1;
        tag = oddMutualTag::odd;
        auto next_n = n - 1;
        n = next_n;
        break;
      }
    }
  }
}

int odd(int n) {
  return odd_dispatch(oddMutualTag::odd, n);
}

int even(int n) {
  return odd_dispatch(oddMutualTag::even, n);
}



