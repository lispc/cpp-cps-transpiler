[Detected recursive function] odd
[Detected recursive function] even

// ================================
// Generated iterative code
// ================================

// === Generated mutual-recursion code ===

enum class oddMutualTag {
  odd,
  even
};

int odd_dispatch(oddMutualTag tag, int cps_p0) {
  while (1) {
    switch (tag) {
      case oddMutualTag::odd:
      {
        if ((cps_p0 == 0)) return 0;
        tag = oddMutualTag::even;
        auto next_cps_p0 = (cps_p0 - 1);
        cps_p0 = next_cps_p0;
        break;
      }
      case oddMutualTag::even:
      {
        if ((cps_p0 == 0)) return 1;
        tag = oddMutualTag::odd;
        auto next_cps_p0 = (cps_p0 - 1);
        cps_p0 = next_cps_p0;
        break;
      }
    }
  }
}

int odd(int cps_p0) {
  return odd_dispatch(oddMutualTag::odd, cps_p0);
}

int even(int cps_p0) {
  return odd_dispatch(oddMutualTag::even, cps_p0);
}


