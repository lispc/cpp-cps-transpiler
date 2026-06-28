Error while trying to load a compilation database:
Could not auto-detect compilation database for file "tests/test_input_tailrec.cc"
No compilation database found in /Users/zhangzhuo/repos/personal/cps/tests or any parent directory
fixed-compilation-database: Error while opening fixed database: No such file or directory
json-compilation-database: Error while opening JSON database: No such file or directory
Running without flags.
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


