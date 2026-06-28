Error while trying to load a compilation database:
Could not auto-detect compilation database for file "tests/test_input_switch3.cc"
No compilation database found in /Users/zhangzhuo/repos/personal/cps/tests or any parent directory
fixed-compilation-database: Error while opening fixed database: No such file or directory
json-compilation-database: Error while opening JSON database: No such file or directory
Running without flags.
[Detected recursive function] fact_switch

// ================================
// Generated iterative code
// ================================

// === Generated accumulator code for function: fact_switch ===

int fact_switch(int n) {
  int product = 1;
  while (!((n == 0)) && !((n == 1))) {
    product = product * n;
    auto next_n = n - 1;
    n = next_n;
  }
  return product;
}


