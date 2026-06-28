Error while trying to load a compilation database:
Could not auto-detect compilation database for file "tests/test_input_xor_acc.cc"
No compilation database found in /Users/zhangzhuo/repos/personal/cps/tests or any parent directory
fixed-compilation-database: Error while opening fixed database: No such file or directory
json-compilation-database: Error while opening JSON database: No such file or directory
Running without flags.
[Detected recursive function] xor_acc

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: xor_acc ===

int xor_acc(int n) {
  int xors = 0;
  while (!(n <= 0)) {
    xors = xors ^ n;
    auto next_n = n - 1;
    n = next_n;
  }
  return xors;
}


