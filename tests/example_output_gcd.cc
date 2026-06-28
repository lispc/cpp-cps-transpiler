Error while trying to load a compilation database:
Could not auto-detect compilation database for file "tests/test_input_gcd.cc"
No compilation database found in /Users/zhangzhuo/repos/personal/cps/tests or any parent directory
fixed-compilation-database: Error while opening fixed database: No such file or directory
json-compilation-database: Error while opening JSON database: No such file or directory
Running without flags.
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


