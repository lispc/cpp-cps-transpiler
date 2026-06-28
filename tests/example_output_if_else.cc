Error while trying to load a compilation database:
Could not auto-detect compilation database for file "tests/test_input_if_else.cc"
No compilation database found in /Users/zhangzhuo/repos/personal/cps/tests or any parent directory
fixed-compilation-database: Error while opening fixed database: No such file or directory
json-compilation-database: Error while opening JSON database: No such file or directory
Running without flags.
[Detected recursive function] if_else

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: if_else ===

int if_else(int n) {
  int product = 1;
  while (!(n <= 1)) {
    product = product * 2;
    auto next_n = n - 1;
    n = next_n;
  }
  return product;
}


