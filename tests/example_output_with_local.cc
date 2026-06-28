Error while trying to load a compilation database:
Could not auto-detect compilation database for file "tests/test_input_with_local.cc"
No compilation database found in /Users/zhangzhuo/repos/personal/cps/tests or any parent directory
fixed-compilation-database: Error while opening fixed database: No such file or directory
json-compilation-database: Error while opening JSON database: No such file or directory
Running without flags.
[Detected recursive function] with_local

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: with_local ===

int with_local(int n) {
  int product = 1;
  while (!(n <= 1)) {
    auto m = n - 1;

    product = product * n;
    auto next_n = m;
    n = next_n;
  }
  return product;
}


