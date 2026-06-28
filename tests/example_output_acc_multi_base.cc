Error while trying to load a compilation database:
Could not auto-detect compilation database for file "tests/test_input_acc_multi_base.cc"
No compilation database found in /Users/zhangzhuo/repos/personal/cps/tests or any parent directory
fixed-compilation-database: Error while opening fixed database: No such file or directory
json-compilation-database: Error while opening JSON database: No such file or directory
Running without flags.
[Detected recursive function] acc_multi_base

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: acc_multi_base ===

int acc_multi_base(int n) {
  int sum = 0;
  while (!(n <= 0) && !(n == 1)) {
    sum = sum + n;
    auto next_n = n - 1;
    n = next_n;
  }
  return sum;
}


