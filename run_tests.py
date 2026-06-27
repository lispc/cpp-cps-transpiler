#!/usr/bin/env python3
"""
Automated regression tests for cps-transpiler.

For each test case in tests/:
  1. Run transpiler on test_input_*.cc
  2. Strip diagnostic comments
  3. Compile generated code with a small main()
  4. Run and verify expected output
"""

import subprocess
import sys
import os
import tempfile

TRANSPILER = "./build/cps-transpiler"

TESTS = [
    {
        "name": "fib",
        "input": "tests/test_input_fib.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 10; ++i) {
    std::cout << "fib(" << i << ") = " << fib(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "fib(0) = 0",
            "fib(1) = 1",
            "fib(2) = 1",
            "fib(3) = 2",
            "fib(4) = 3",
            "fib(5) = 5",
            "fib(6) = 8",
            "fib(7) = 13",
            "fib(8) = 21",
            "fib(9) = 34",
            "fib(10) = 55",
        ],
    },
    {
        "name": "fact",
        "input": "tests/test_input_fact.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 10; ++i) {
    std::cout << "fact(" << i << ") = " << fact(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "fact(0) = 1",
            "fact(1) = 1",
            "fact(2) = 2",
            "fact(3) = 6",
            "fact(4) = 24",
            "fact(5) = 120",
            "fact(6) = 720",
            "fact(7) = 5040",
            "fact(8) = 40320",
            "fact(9) = 362880",
            "fact(10) = 3628800",
        ],
    },
    {
        "name": "tailrec",
        "input": "tests/test_input_tailrec.cc",
        "main": """
#include <iostream>
int main() {
  std::cout << "clamp_down(15) = " << clamp_down(15) << std::endl;
  std::cout << "clamp_down(100) = " << clamp_down(100) << std::endl;
  return 0;
}
""",
        "expected": [
            "clamp_down(15) = 10",
            "clamp_down(100) = 10",
        ],
    },
    {
        "name": "funcwrap",
        "input": "tests/test_input_funcwrap.cc",
        "preamble": "int double_it(int x) { return x * 2; }\n",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 10; ++i) {
    std::cout << "double_fact(" << i << ") = " << double_fact(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "double_fact(0) = 1",
            "double_fact(1) = 1",
            "double_fact(2) = 2",
            "double_fact(3) = 4",
            "double_fact(4) = 8",
            "double_fact(5) = 16",
            "double_fact(6) = 32",
            "double_fact(7) = 64",
            "double_fact(8) = 128",
            "double_fact(9) = 256",
            "double_fact(10) = 512",
        ],
    },
    {
        "name": "unary",
        "input": "tests/test_input_unary.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 6; ++i) {
    std::cout << "neg_fact(" << i << ") = " << neg_fact(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "neg_fact(0) = -1",
            "neg_fact(1) = -1",
            "neg_fact(2) = 1",
            "neg_fact(3) = -1",
            "neg_fact(4) = 1",
            "neg_fact(5) = -1",
            "neg_fact(6) = 1",
        ],
    },
    {
        "name": "gcd",
        "input": "tests/test_input_gcd.cc",
        "main": """
#include <iostream>
int main() {
  std::cout << "gcd(48, 18) = " << gcd(48, 18) << std::endl;
  std::cout << "gcd(100, 35) = " << gcd(100, 35) << std::endl;
  std::cout << "gcd(7, 5) = " << gcd(7, 5) << std::endl;
  return 0;
}
""",
        "expected": [
            "gcd(48, 18) = 6",
            "gcd(100, 35) = 5",
            "gcd(7, 5) = 1",
        ],
    },
    {
        "name": "multi_cps",
        "input": "tests/test_input_multi_cps.cc",
        "main": """
#include <iostream>
int main() {
  std::cout << "multi_cps(5, 10) = " << multi_cps(5, 10) << std::endl;
  std::cout << "multi_cps(6, 10) = " << multi_cps(6, 10) << std::endl;
  return 0;
}
""",
        "expected": [
            "multi_cps(5, 10) = 130",
            "multi_cps(6, 10) = 210",
        ],
    },
    {
        "name": "ptr",
        "input": "tests/test_input_ptr.cc",
        "main": """
#include <iostream>
int main() {
  int arr[] = {1, 2, 3, 4, 5};
  std::cout << "sum_ptr(arr, 5) = " << sum_ptr(arr, 5) << std::endl;
  return 0;
}
""",
        "expected": [
            "sum_ptr(arr, 5) = 15",
        ],
    },
    {
        "name": "ref_cps",
        "input": "tests/test_input_ref_cps.cc",
        "main": """
#include <iostream>
int main() {
  int x = 5;
  std::cout << "ref_sum(x=5, 3) = " << ref_sum(x, 3) << std::endl;
  std::cout << "ref_sum(x=5, 5) = " << ref_sum(x, 5) << std::endl;
  return 0;
}
""",
        "expected": [
            "ref_sum(x=5, 3) = 15",
            "ref_sum(x=5, 5) = 25",
        ],
    },
    {
        "name": "ref_tro",
        "input": "tests/test_input_ref.cc",
        "main": """
#include <iostream>
int main() {
  int y = 5;
  std::cout << "ref_accumulate(y=5, 3) = " << ref_accumulate(y, 3) << std::endl;
  std::cout << "y after = " << y << std::endl;
  return 0;
}
""",
        "expected": [
            "ref_accumulate(y=5, 3) = 8",
            "y after = 8",
        ],
    },
    {
        "name": "weird",
        "input": "tests/test_input_weird.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "weird(" << i << ") = " << weird(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "weird(0) = 0",
            "weird(1) = 1",
            "weird(2) = 1",
            "weird(3) = 3",
            "weird(4) = 5",
            "weird(5) = 11",
            "weird(6) = 21",
            "weird(7) = 43",
            "weird(8) = 85",
        ],
    },
    {
        "name": "if_else",
        "input": "tests/test_input_if_else.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 6; ++i) {
    std::cout << "if_else(" << i << ") = " << if_else(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "if_else(0) = 1",
            "if_else(1) = 1",
            "if_else(2) = 2",
            "if_else(3) = 4",
            "if_else(4) = 8",
            "if_else(5) = 16",
            "if_else(6) = 32",
        ],
    },
    {
        "name": "multi_base",
        "input": "tests/test_input_multi_base.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "multi_base(" << i << ") = " << multi_base(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "multi_base(0) = 0",
            "multi_base(1) = 1",
            "multi_base(2) = 1",
            "multi_base(3) = 2",
            "multi_base(4) = 3",
            "multi_base(5) = 5",
            "multi_base(6) = 8",
            "multi_base(7) = 13",
            "multi_base(8) = 21",
        ],
    },
    {
        "name": "with_local",
        "input": "tests/test_input_with_local.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "with_local(" << i << ") = " << with_local(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "with_local(0) = 1",
            "with_local(1) = 1",
            "with_local(2) = 2",
            "with_local(3) = 6",
            "with_local(4) = 24",
            "with_local(5) = 120",
            "with_local(6) = 720",
            "with_local(7) = 5040",
            "with_local(8) = 40320",
        ],
    },
    {
        "name": "xor_acc",
        "input": "tests/test_input_xor_acc.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "xor_acc(" << i << ") = " << xor_acc(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "xor_acc(0) = 0",
            "xor_acc(1) = 1",
            "xor_acc(2) = 3",
            "xor_acc(3) = 0",
            "xor_acc(4) = 4",
            "xor_acc(5) = 1",
            "xor_acc(6) = 7",
            "xor_acc(7) = 0",
            "xor_acc(8) = 8",
        ],
    },
    {
        "name": "xor_tree",
        "input": "tests/test_input_xor_tree.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "xor_tree(" << i << ") = " << xor_tree(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "xor_tree(0) = 0",
            "xor_tree(1) = 1",
            "xor_tree(2) = 1",
            "xor_tree(3) = 0",
            "xor_tree(4) = 1",
            "xor_tree(5) = 1",
            "xor_tree(6) = 0",
            "xor_tree(7) = 1",
            "xor_tree(8) = 1",
        ],
    },
    {
        "name": "minmax",
        "input": "tests/test_input_minmax.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 6; ++i) {
    std::cout << "min_tree(" << i << ") = " << min_tree(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "min_tree(0) = 10",
            "min_tree(1) = 1",
            "min_tree(2) = 1",
            "min_tree(3) = 1",
            "min_tree(4) = 1",
            "min_tree(5) = 1",
            "min_tree(6) = 1",
        ],
    },
    {
        "name": "acc_multi_base",
        "input": "tests/test_input_acc_multi_base.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "acc_multi_base(" << i << ") = " << acc_multi_base(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "acc_multi_base(0) = 0",
            "acc_multi_base(1) = 0",
            "acc_multi_base(2) = 2",
            "acc_multi_base(3) = 5",
            "acc_multi_base(4) = 9",
            "acc_multi_base(5) = 14",
            "acc_multi_base(6) = 20",
            "acc_multi_base(7) = 27",
            "acc_multi_base(8) = 35",
        ],
    },
    {
        "name": "showcase",
        "input": "tests/test_input_showcase.cc",
        "main": """
#include <iostream>
int main() {
  std::cout << "C(0, 0) = " << binomial(0, 0) << std::endl;
  std::cout << "C(4, 2) = " << binomial(4, 2) << std::endl;
  std::cout << "C(5, 2) = " << binomial(5, 2) << std::endl;
  std::cout << "C(6, 3) = " << binomial(6, 3) << std::endl;
  std::cout << "C(10, 5) = " << binomial(10, 5) << std::endl;
  std::cout << "C(20, 10) = " << binomial(20, 10) << std::endl;
  return 0;
}
""",
        "expected": [
            "C(0, 0) = 1",
            "C(4, 2) = 6",
            "C(5, 2) = 10",
            "C(6, 3) = 20",
            "C(10, 5) = 252",
            "C(20, 10) = 184756",
        ],
    },
    {
        "name": "switch_fib",
        "input": "tests/test_input_switch.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 10; ++i) {
    std::cout << "fib_switch(" << i << ") = " << fib_switch(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "fib_switch(0) = 0",
            "fib_switch(1) = 1",
            "fib_switch(2) = 1",
            "fib_switch(3) = 2",
            "fib_switch(4) = 3",
            "fib_switch(5) = 5",
            "fib_switch(6) = 8",
            "fib_switch(7) = 13",
            "fib_switch(8) = 21",
            "fib_switch(9) = 34",
            "fib_switch(10) = 55",
        ],
    },
    {
        "name": "switch_fact",
        "input": "tests/test_input_switch3.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "fact_switch(" << i << ") = " << fact_switch(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "fact_switch(0) = 1",
            "fact_switch(1) = 1",
            "fact_switch(2) = 2",
            "fact_switch(3) = 6",
            "fact_switch(4) = 24",
            "fact_switch(5) = 120",
            "fact_switch(6) = 720",
            "fact_switch(7) = 5040",
            "fact_switch(8) = 40320",
        ],
    },
    {
        "name": "impure_base",
        "input": "tests/test_input_impure_base.cc",
        "preamble": "int counter = 0;\n",
        "main": """
#include <iostream>
int main() {
  std::cout << "impure_base(5) = " << impure_base(5) << std::endl;
  return 0;
}
""",
        "expected": [
            "impure_base(5) = 15",
        ],
    },
    {
        "name": "nested",
        "input": "tests/test_input_nested.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 4; ++i) {
    std::cout << "nested_fact(" << i << ") = " << nested_fact(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "nested_fact(0) = 1",
            "nested_fact(1) = 1",
            "nested_fact(2) = 1",
            "nested_fact(3) = 1",
            "nested_fact(4) = 1",
        ],
    },
    {
        "name": "mutual",
        "input": "tests/test_input_mutual.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 6; ++i) {
    std::cout << "odd(" << i << ") = " << odd(i) << ", even(" << i << ") = " << even(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "odd(0) = 0, even(0) = 1",
            "odd(1) = 1, even(1) = 0",
            "odd(2) = 0, even(2) = 1",
            "odd(3) = 1, even(3) = 0",
            "odd(4) = 0, even(4) = 1",
            "odd(5) = 1, even(5) = 0",
            "odd(6) = 0, even(6) = 1",
        ],
    },
    {
        "name": "showcase2",
        "input": "tests/test_input_showcase2.cc",
        "main": """
#include <iostream>
int main() {
  std::cout << "mc91(50) = " << mc91(50) << std::endl;
  std::cout << "mc91(101) = " << mc91(101) << std::endl;
  std::cout << "mc91(110) = " << mc91(110) << std::endl;
  for (int i = 0; i <= 8; ++i) {
    std::cout << "mod0(" << i << ") = " << mod0(i) << ", mod1(" << i << ") = " << mod1(i) << ", mod2(" << i << ") = " << mod2(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "mc91(50) = 91",
            "mc91(101) = 91",
            "mc91(110) = 100",
            "mod0(0) = 1, mod1(0) = 0, mod2(0) = 0",
            "mod0(1) = 0, mod1(1) = 1, mod2(1) = 0",
            "mod0(2) = 0, mod1(2) = 0, mod2(2) = 1",
            "mod0(3) = 1, mod1(3) = 0, mod2(3) = 0",
            "mod0(4) = 0, mod1(4) = 1, mod2(4) = 0",
            "mod0(5) = 0, mod1(5) = 0, mod2(5) = 1",
            "mod0(6) = 1, mod1(6) = 0, mod2(6) = 0",
            "mod0(7) = 0, mod1(7) = 1, mod2(7) = 0",
            "mod0(8) = 0, mod1(8) = 0, mod2(8) = 1",
        ],
    },
    {
        "name": "memo",
        "input": "tests/test_input_memo.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "memo_weird(" << i << ") = " << memo_weird(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "memo_weird(0) = 0",
            "memo_weird(1) = 1",
            "memo_weird(2) = 2",
            "memo_weird(3) = 5",
            "memo_weird(4) = 10",
            "memo_weird(5) = 21",
            "memo_weird(6) = 42",
            "memo_weird(7) = 85",
            "memo_weird(8) = 170",
        ],
    },
    {
        "name": "types",
        "input": "tests/test_input_types.cc",
        "main": """
#include <iostream>
int main() {
  std::cout << "fib_ll(10) = " << fib_ll(10) << std::endl;
  std::cout << "fact_unsigned(5) = " << fact_unsigned(5) << std::endl;
  for (unsigned i = 0; i <= 5; ++i) {
    std::cout << "is_even(" << i << ") = " << is_even(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "fib_ll(10) = 55",
            "fact_unsigned(5) = 120",
            "is_even(0) = 1",
            "is_even(1) = 0",
            "is_even(2) = 1",
            "is_even(3) = 0",
            "is_even(4) = 1",
            "is_even(5) = 0",
        ],
    },
    {
        "name": "ternary",
        "input": "tests/test_input_ternary.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "fact_t(" << i << ") = " << fact_t(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "fact_t(0) = 1",
            "fact_t(1) = 1",
            "fact_t(2) = 2",
            "fact_t(3) = 6",
            "fact_t(4) = 24",
            "fact_t(5) = 120",
            "fact_t(6) = 720",
            "fact_t(7) = 5040",
            "fact_t(8) = 40320",
        ],
    },
    {
        "name": "guard",
        "input": "tests/test_input_guard.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = -3; i <= 8; ++i) {
    std::cout << "fact_guard(" << i << ") = " << fact_guard(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "fact_guard(-3) = -1",
            "fact_guard(-2) = -1",
            "fact_guard(-1) = -1",
            "fact_guard(0) = 1",
            "fact_guard(1) = 1",
            "fact_guard(2) = 2",
            "fact_guard(3) = 6",
            "fact_guard(4) = 24",
            "fact_guard(5) = 120",
            "fact_guard(6) = 720",
            "fact_guard(7) = 5040",
            "fact_guard(8) = 40320",
        ],
    },
    {
        "name": "void",
        "input": "tests/test_input_void.cc",
        "preamble": "int emitted[100];\nint emitted_count = 0;\nvoid emit(int x) { emitted[emitted_count++] = x; }\n",
        "main": """
#include <iostream>
int main() {
  print_down(5);
  for (int i = 0; i < emitted_count; ++i) {
    std::cout << "emit[" << i << "] = " << emitted[i] << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "emit[0] = 5",
            "emit[1] = 4",
            "emit[2] = 3",
            "emit[3] = 2",
            "emit[4] = 1",
        ],
    },
    {
        "name": "mutual_nontail",
        "input": "tests/test_input_mutual_nontail.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 6; ++i) {
    std::cout << "f(" << i << ") = " << f(i) << ", g(" << i << ") = " << g(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "f(0) = 1, g(0) = 0",
            "f(1) = 1, g(1) = 1",
            "f(2) = 2, g(2) = 1",
            "f(3) = 2, g(3) = 2",
            "f(4) = 3, g(4) = 2",
            "f(5) = 3, g(5) = 3",
            "f(6) = 4, g(6) = 3",
        ],
    },
    {
        "name": "sub",
        "input": "tests/test_input_sub.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "sub_acc(" << i << ") = " << sub_acc(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "sub_acc(0) = 1",
            "sub_acc(1) = 1",
            "sub_acc(2) = -1",
            "sub_acc(3) = -4",
            "sub_acc(4) = -8",
            "sub_acc(5) = -13",
            "sub_acc(6) = -19",
            "sub_acc(7) = -26",
            "sub_acc(8) = -34",
        ],
    },
    {
        "name": "middle_loop",
        "input": "tests/test_input_middle_loop.cc",
        "main": """
#include <iostream>
int main() {
  for (int i = 0; i <= 8; ++i) {
    std::cout << "sum_loop(" << i << ") = " << sum_loop(i) << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "sum_loop(0) = 0",
            "sum_loop(1) = 1",
            "sum_loop(2) = 2",
            "sum_loop(3) = 5",
            "sum_loop(4) = 11",
            "sum_loop(5) = 21",
            "sum_loop(6) = 36",
            "sum_loop(7) = 57",
            "sum_loop(8) = 85",
        ],
    },
    {
        "name": "dogfood",
        "input": "tests/test_input_dogfood.cc",
        "preamble": "struct Node {\n  int tag;\n  Node *left;\n  Node *right;\n};\n",
        "main": """
#include <iostream>
int main() {
  Node n4{4, nullptr, nullptr};
  Node n5{5, nullptr, nullptr};
  Node n6{6, nullptr, nullptr};
  Node n7{7, nullptr, nullptr};
  Node n2{2, &n4, &n5};
  Node n3{3, &n6, &n7};
  Node root{1, &n2, &n3};
  std::cout << "contains_target(root, 5) = " << contains_target(&root, 5) << std::endl;
  std::cout << "contains_target(root, 8) = " << contains_target(&root, 8) << std::endl;
  std::cout << "contains_target(nullptr, 1) = " << contains_target(nullptr, 1) << std::endl;
  return 0;
}
""",
        "expected": [
            "contains_target(root, 5) = 1",
            "contains_target(root, 8) = 0",
            "contains_target(nullptr, 1) = 0",
        ],
    },
    {
        "name": "tree_search",
        "input": "tests/test_input_tree_search.cc",
        "preamble": "struct Node {\n  int tag;\n  Node *children[2];\n  int num_children;\n};\n",
        "main": """
#include <iostream>
int main() {
  Node n4{4, {nullptr, nullptr}, 0};
  Node n5{5, {nullptr, nullptr}, 0};
  Node n6{6, {nullptr, nullptr}, 0};
  Node n7{7, {nullptr, nullptr}, 0};
  Node *c2[] = {&n4, &n5};
  Node *c3[] = {&n6, &n7};
  Node n2{2, {c2[0], c2[1]}, 2};
  Node n3{3, {c3[0], c3[1]}, 2};
  Node *croot[] = {&n2, &n3};
  Node root{1, {croot[0], croot[1]}, 2};
  std::cout << "tree_search(root, 5) = " << tree_search(&root, 5) << std::endl;
  std::cout << "tree_search(root, 8) = " << tree_search(&root, 8) << std::endl;
  std::cout << "tree_search(nullptr, 1) = " << tree_search(nullptr, 1) << std::endl;
  return 0;
}
""",
        "expected": [
            "tree_search(root, 5) = 1",
            "tree_search(root, 8) = 0",
            "tree_search(nullptr, 1) = 0",
        ],
    },
    {
        "name": "collect_calls",
        "input": "tests/test_input_collect_calls.cc",
        "preamble": "struct Node;\n\nstruct NodePtrList {\n  Node *data[10];\n  int size;\n  NodePtrList() : size(0) {}\n  void push_back(Node *x) { data[size++] = x; }\n  Node **begin() { return data; }\n  Node **end() { return data + size; }\n\n  struct RevIt {\n    Node **p;\n    Node *&operator*() { return *(p - 1); }\n    RevIt &operator++() { --p; return *this; }\n    bool operator!=(const RevIt &o) const { return p != o.p; }\n  };\n  RevIt rbegin() { return RevIt{data + size}; }\n  RevIt rend() { return RevIt{data}; }\n\n  struct CRevIt {\n    Node *const *p;\n    Node *const &operator*() { return *(p - 1); }\n    CRevIt &operator++() { --p; return *this; }\n    bool operator!=(const CRevIt &o) const { return p != o.p; }\n  };\n  CRevIt rbegin() const { return CRevIt{data + size}; }\n  CRevIt rend() const { return CRevIt{data}; }\n};\n\nstruct Node {\n  int kind;\n  int func_id;\n  Node *child_arr[2];\n  int child_count;\n  NodePtrList children() const {\n    NodePtrList list;\n    for (int i = 0; i < child_count; ++i)\n      list.push_back(child_arr[i]);\n    return list;\n  }\n};\n",
        "main": """
#include <iostream>
int main() {
  Node n4{1, 7, {nullptr, nullptr}, 0};
  Node n5{1, 7, {nullptr, nullptr}, 0};
  Node n2{0, 0, {&n4, &n5}, 2};
  Node n6{1, 7, {nullptr, nullptr}, 0};
  Node n3{0, 0, {&n6, nullptr}, 1};
  Node root{0, 0, {&n2, &n3}, 2};
  NodePtrList calls;
  collect_calls(&root, 7, calls);
  for (int i = 0; i < calls.size; ++i) {
    std::cout << "call[" << i << "] = " << calls.data[i]->func_id << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "call[0] = 7",
            "call[1] = 7",
            "call[2] = 7",
        ],
    },
    {
        "name": "find_target",
        "input": "tests/test_input_find_target.cc",
        "preamble": "struct Node;\n\nstruct NodePtrList {\n  Node *data[10];\n  int size;\n  NodePtrList() : size(0) {}\n  void push_back(Node *x) { data[size++] = x; }\n  Node **begin() { return data; }\n  Node **end() { return data + size; }\n\n  struct RevIt {\n    Node **p;\n    Node *&operator*() { return *(p - 1); }\n    RevIt &operator++() { --p; return *this; }\n    bool operator!=(const RevIt &o) const { return p != o.p; }\n  };\n  RevIt rbegin() { return RevIt{data + size}; }\n  RevIt rend() { return RevIt{data}; }\n\n  struct CRevIt {\n    Node *const *p;\n    Node *const &operator*() { return *(p - 1); }\n    CRevIt &operator++() { --p; return *this; }\n    bool operator!=(const CRevIt &o) const { return p != o.p; }\n  };\n  CRevIt rbegin() const { return CRevIt{data + size}; }\n  CRevIt rend() const { return CRevIt{data}; }\n};\n\nstruct Node {\n  int kind;\n  int value;\n  Node *child_arr[2];\n  int child_count;\n  NodePtrList children() const {\n    NodePtrList list;\n    for (int i = 0; i < child_count; ++i)\n      list.push_back(child_arr[i]);\n    return list;\n  }\n};\n",
        "main": """
#include <iostream>
int main() {
  Node n4{1, 7, {nullptr, nullptr}, 0};
  Node n5{1, 9, {nullptr, nullptr}, 0};
  Node n2{0, 0, {&n4, &n5}, 2};
  Node n6{1, 7, {nullptr, nullptr}, 0};
  Node n3{0, 0, {&n6, nullptr}, 1};
  Node root{0, 0, {&n2, &n3}, 2};
  int out = -1;
  const Node *found = find_target(&root, 7, out);
  std::cout << "found = " << (found ? found->value : -1)
            << " out = " << out << std::endl;
  return 0;
}
""",
        "expected": [
            "found = 7 out = 7",
        ],
    },
    {
        "name": "collect_deep",
        "input": "tests/test_input_collect_deep.cc",
        "preamble": "struct Node;\n\nstruct NodePtrList {\n  Node *data[10];\n  int size;\n  NodePtrList() : size(0) {}\n  void push_back(Node *x) { data[size++] = x; }\n  Node **begin() { return data; }\n  Node **end() { return data + size; }\n\n  struct RevIt {\n    Node **p;\n    Node *&operator*() { return *(p - 1); }\n    RevIt &operator++() { --p; return *this; }\n    bool operator!=(const RevIt &o) const { return p != o.p; }\n  };\n  RevIt rbegin() { return RevIt{data + size}; }\n  RevIt rend() { return RevIt{data}; }\n\n  struct CRevIt {\n    Node *const *p;\n    Node *const &operator*() { return *(p - 1); }\n    CRevIt &operator++() { --p; return *this; }\n    bool operator!=(const CRevIt &o) const { return p != o.p; }\n  };\n  CRevIt rbegin() const { return CRevIt{data + size}; }\n  CRevIt rend() const { return CRevIt{data}; }\n};\n\nstruct Node {\n  int kind;\n  int value;\n  Node *child_arr[2];\n  int child_count;\n  NodePtrList children() const {\n    NodePtrList list;\n    for (int i = 0; i < child_count; ++i)\n      list.push_back(child_arr[i]);\n    return list;\n  }\n};\n",
        "main": """
#include <iostream>
int main() {
  Node n4{1, 4, {nullptr, nullptr}, 0};
  Node n5{0, 5, {nullptr, nullptr}, 0};
  Node n2{0, 2, {&n4, &n5}, 2};
  Node n6{1, 6, {nullptr, nullptr}, 0};
  Node n3{0, 3, {&n6, nullptr}, 1};
  Node root{0, 1, {&n2, &n3}, 2};
  NodePtrList calls;
  collect_deep(&root, calls);
  for (int i = 0; i < calls.size; ++i) {
    std::cout << "call[" << i << "] = " << calls.data[i]->value << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "call[0] = 4",
            "call[1] = 6",
        ],
    },
    {
        "name": "expr_uses",
        "input": "tests/test_input_expr_uses.cc",
        "preamble": "struct Node;\n\nstruct NodePtrList {\n  Node *data[10];\n  int size;\n  NodePtrList() : size(0) {}\n  void push_back(Node *x) { data[size++] = x; }\n  Node **begin() { return data; }\n  Node **end() { return data + size; }\n\n  struct RevIt {\n    Node **p;\n    Node *&operator*() { return *(p - 1); }\n    RevIt &operator++() { --p; return *this; }\n    bool operator!=(const RevIt &o) const { return p != o.p; }\n  };\n  RevIt rbegin() { return RevIt{data + size}; }\n  RevIt rend() { return RevIt{data}; }\n\n  struct CRevIt {\n    Node *const *p;\n    Node *const &operator*() { return *(p - 1); }\n    CRevIt &operator++() { --p; return *this; }\n    bool operator!=(const CRevIt &o) const { return p != o.p; }\n  };\n  CRevIt rbegin() const { return CRevIt{data + size}; }\n  CRevIt rend() const { return CRevIt{data}; }\n};\n\nstruct Node {\n  int value;\n  Node *child_arr[2];\n  int child_count;\n  NodePtrList children() const {\n    NodePtrList list;\n    for (int i = 0; i < child_count; ++i)\n      list.push_back(child_arr[i]);\n    return list;\n  }\n};\n",
        "main": """
#include <iostream>
int main() {
  Node n4{4, {nullptr, nullptr}, 0};
  Node n5{5, {nullptr, nullptr}, 0};
  Node n2{2, {&n4, &n5}, 2};
  Node n6{7, {nullptr, nullptr}, 0};
  Node n3{3, {&n6, nullptr}, 1};
  Node root{1, {&n2, &n3}, 2};
  std::cout << "expr_uses(root, 7) = " << expr_uses(&root, 7) << std::endl;
  std::cout << "expr_uses(root, 9) = " << expr_uses(&root, 9) << std::endl;
  return 0;
}
""",
        "expected": [
            "expr_uses(root, 7) = 1",
            "expr_uses(root, 9) = 0",
        ],
    },
    {
        "name": "contains_rec",
        "input": "tests/test_input_contains_rec.cc",
        "preamble": "struct Node;\n\nstruct NodePtrList {\n  Node *data[10];\n  int size;\n  NodePtrList() : size(0) {}\n  void push_back(Node *x) { data[size++] = x; }\n  Node **begin() { return data; }\n  Node **end() { return data + size; }\n\n  struct RevIt {\n    Node **p;\n    Node *&operator*() { return *(p - 1); }\n    RevIt &operator++() { --p; return *this; }\n    bool operator!=(const RevIt &o) const { return p != o.p; }\n  };\n  RevIt rbegin() { return RevIt{data + size}; }\n  RevIt rend() { return RevIt{data}; }\n\n  struct CRevIt {\n    Node *const *p;\n    Node *const &operator*() { return *(p - 1); }\n    CRevIt &operator++() { --p; return *this; }\n    bool operator!=(const CRevIt &o) const { return p != o.p; }\n  };\n  CRevIt rbegin() const { return CRevIt{data + size}; }\n  CRevIt rend() const { return CRevIt{data}; }\n};\n\nstruct Node {\n  int kind;\n  int func_id;\n  Node *child_arr[2];\n  int child_count;\n  NodePtrList children() const {\n    NodePtrList list;\n    for (int i = 0; i < child_count; ++i)\n      list.push_back(child_arr[i]);\n    return list;\n  }\n};\n",
        "main": """
#include <iostream>
int main() {
  Node n4{1, 7, {nullptr, nullptr}, 0};
  Node n5{0, 9, {nullptr, nullptr}, 0};
  Node n2{0, 0, {&n4, &n5}, 2};
  Node n6{1, 7, {nullptr, nullptr}, 0};
  Node n3{0, 0, {&n6, nullptr}, 1};
  Node root{0, 0, {&n2, &n3}, 2};
  std::cout << "contains_rec(root, 7) = " << contains_rec(&root, 7) << std::endl;
  std::cout << "contains_rec(root, 8) = " << contains_rec(&root, 8) << std::endl;
  return 0;
}
""",
        "expected": [
            "contains_rec(root, 7) = 1",
            "contains_rec(root, 8) = 0",
        ],
    },
    {
        "name": "is_pure",
        "input": "tests/test_input_is_pure.cc",
        "preamble": "struct Expr {\n  int kind;\n  Expr *args[3];\n  int arg_count;\n};\n",
        "main": """
#include <iostream>
int main() {
  Expr a{0, {nullptr, nullptr, nullptr}, 0};
  Expr b{0, {nullptr, nullptr, nullptr}, 0};
  Expr c{1, {nullptr, nullptr, nullptr}, 0}; // impure call
  Expr n2{2, {&a, &b, nullptr}, 2};
  Expr root1{2, {&n2, &c, nullptr}, 2};
  Expr root2{2, {&n2, &a, nullptr}, 2};
  std::cout << "is_pure(root1) = " << is_pure(&root1) << std::endl;
  std::cout << "is_pure(root2) = " << is_pure(&root2) << std::endl;
  return 0;
}
""",
        "expected": [
            "is_pure(root1) = 0",
            "is_pure(root2) = 1",
        ],
    },
    {
        "name": "is_pure_expr_original",
        "input": "tests/test_input_is_pure_expr.cc",
        "preamble": "#include <string>\n\nstruct FunctionDecl {\n  std::string Name;\n  bool IsConst;\n  FunctionDecl(const std::string &N, bool C = false) : Name(N), IsConst(C) {}\n  std::string getNameAsString() const { return Name; }\n  bool isConst() const { return IsConst; }\n};\n\nstruct StmtRange;\n\nstruct Stmt {\n  virtual ~Stmt() = default;\n  virtual StmtRange children() const;\n};\n\nenum Opcode {\n  BO_Add,\n  BO_Sub,\n  BO_Comma,\n  BO_Assign\n};\n\nenum UOpcode {\n  UO_LNot,\n  UO_PreInc,\n  UO_PreDec,\n  UO_PostInc,\n  UO_PostDec\n};\n\nstruct StmtRange {\n  static constexpr int Max = 16;\n  Stmt *Items[Max];\n  int Size;\n  StmtRange() : Size(0) {}\n  void push_back(Stmt *S) { Items[Size++] = S; }\n\n  Stmt **begin() { return Items; }\n  Stmt **end() { return Items + Size; }\n  Stmt *const *begin() const { return Items; }\n  Stmt *const *end() const { return Items + Size; }\n\n  struct RevIt {\n    Stmt **P;\n    Stmt *&operator*() { return *(P - 1); }\n    RevIt &operator++() { --P; return *this; }\n    bool operator!=(const RevIt &O) const { return P != O.P; }\n  };\n  struct ConstRevIt {\n    Stmt *const *P;\n    Stmt *const &operator*() { return *(P - 1); }\n    ConstRevIt &operator++() { --P; return *this; }\n    bool operator!=(const ConstRevIt &O) const { return P != O.P; }\n  };\n  RevIt rbegin() { return RevIt{Items + Size}; }\n  RevIt rend() { return RevIt{Items}; }\n  ConstRevIt rbegin() const { return ConstRevIt{Items + Size}; }\n  ConstRevIt rend() const { return ConstRevIt{Items}; }\n};\n\nStmtRange Stmt::children() const { return StmtRange(); }\n\nstruct Expr : Stmt {\n  virtual const Expr *IgnoreParenImpCasts() const { return this; }\n  virtual StmtRange children() const override { return StmtRange(); }\n};\n\nstruct CallExpr : Expr {\n  FunctionDecl *Callee;\n  StmtRange Args;\n  CallExpr(FunctionDecl *C) : Callee(C) {}\n  void addArg(Expr *A) { Args.push_back(A); }\n  const FunctionDecl *getDirectCallee() const { return Callee; }\n  StmtRange children() const override {\n    StmtRange R;\n    for (Stmt *S : Args)\n      R.push_back(S);\n    return R;\n  }\n};\n\nstruct BinaryOperator : Expr {\n  Opcode Op;\n  Expr *LHS;\n  Expr *RHS;\n  BinaryOperator(Opcode O, Expr *L, Expr *R) : Op(O), LHS(L), RHS(R) {}\n  Opcode getOpcode() const { return Op; }\n  bool isAssignmentOp() const { return Op == BO_Assign; }\n  const Expr *getLHS() const { return LHS; }\n  const Expr *getRHS() const { return RHS; }\n  StmtRange children() const override {\n    StmtRange R;\n    R.push_back(LHS);\n    R.push_back(RHS);\n    return R;\n  }\n};\n\nstruct UnaryOperator : Expr {\n  UOpcode Op;\n  Expr *Sub;\n  UnaryOperator(UOpcode O, Expr *S) : Op(O), Sub(S) {}\n  UOpcode getOpcode() const { return Op; }\n  bool isIncrementDecrementOp() const {\n    return Op == UO_PreInc || Op == UO_PreDec ||\n           Op == UO_PostInc || Op == UO_PostDec;\n  }\n  const Expr *getSubExpr() const { return Sub; }\n  StmtRange children() const override {\n    StmtRange R;\n    R.push_back(Sub);\n    return R;\n  }\n};\n\ntemplate <typename T, typename U>\nconst T *dyn_cast(const U *P) {\n  return dynamic_cast<const T *>(P);\n}\n\ntemplate <typename T, typename U>\nconst T *dyn_cast_or_null(const U *P) {\n  return P ? dynamic_cast<const T *>(P) : nullptr;\n}\n",
        "main": """
#include <iostream>
int main() {
  FunctionDecl pureFn("pure", true);
  FunctionDecl impureFn("impure", false);
  Expr leaf;
  CallExpr pureCall(&pureFn);
  pureCall.addArg(&leaf);
  CallExpr impureCall(&impureFn);
  impureCall.addArg(&leaf);
  BinaryOperator assign(BO_Assign, &leaf, &leaf);
  BinaryOperator add(BO_Add, &leaf, &leaf);
  BinaryOperator addAssign(BO_Add, &leaf, &assign);
  UnaryOperator inc(UO_PreInc, &leaf);
  BinaryOperator comma(BO_Comma, &leaf, &leaf);
  std::cout << "leaf = " << IsPureExprImpl(&leaf) << std::endl;
  std::cout << "pureCall = " << IsPureExprImpl(&pureCall) << std::endl;
  std::cout << "impureCall = " << IsPureExprImpl(&impureCall) << std::endl;
  std::cout << "assign = " << IsPureExprImpl(&assign) << std::endl;
  std::cout << "add = " << IsPureExprImpl(&add) << std::endl;
  std::cout << "addAssign = " << IsPureExprImpl(&addAssign) << std::endl;
  std::cout << "inc = " << IsPureExprImpl(&inc) << std::endl;
  std::cout << "comma = " << IsPureExprImpl(&comma) << std::endl;
  return 0;
}
""",
        "expected": [
            "leaf = 1",
            "pureCall = 1",
            "impureCall = 0",
            "assign = 0",
            "add = 1",
            "addAssign = 0",
            "inc = 0",
            "comma = 0",
        ],
    },
    {
        "name": "is_tail",
        "input": "tests/test_input_is_tail.cc",
        "preamble": "enum {\n  ST_RETURN,\n  ST_EXPR,\n  ST_IF\n};\n\nstruct Expr {\n  int id;\n};\n\nstruct Stmt {\n  int kind;\n  Expr *ret;\n  Expr *expr;\n  Stmt *then_;\n  Stmt *else_;\n};\n",
        "main": """
#include <iostream>
int main() {
  Expr e1{1}, e2{2}, e3{3};
  Stmt ret_e1{ST_RETURN, &e1, nullptr, nullptr, nullptr};
  Stmt expr_e2{ST_EXPR, nullptr, &e2, nullptr, nullptr};
  Stmt if1{ST_IF, nullptr, nullptr, &ret_e1, &expr_e2}; // tail for e1 and e2
  Stmt if2{ST_IF, nullptr, nullptr, &expr_e2, &expr_e2}; // not tail for e1
  Stmt deep{ST_IF, nullptr, nullptr, &if1, &ret_e1}; // tail for e1
  std::cout << "is_tail(&e1, &ret_e1) = " << is_tail(&e1, &ret_e1) << std::endl;
  std::cout << "is_tail(&e2, &ret_e1) = " << is_tail(&e2, &ret_e1) << std::endl;
  std::cout << "is_tail(&e1, &if1) = " << is_tail(&e1, &if1) << std::endl;
  std::cout << "is_tail(&e3, &if1) = " << is_tail(&e3, &if1) << std::endl;
  std::cout << "is_tail(&e1, &if2) = " << is_tail(&e1, &if2) << std::endl;
  std::cout << "is_tail(&e1, &deep) = " << is_tail(&e1, &deep) << std::endl;
  return 0;
}
""",
        "expected": [
            "is_tail(&e1, &ret_e1) = 1",
            "is_tail(&e2, &ret_e1) = 0",
            "is_tail(&e1, &if1) = 1",
            "is_tail(&e3, &if1) = 0",
            "is_tail(&e1, &if2) = 0",
            "is_tail(&e1, &deep) = 1",
        ],
    },
    {
        "name": "contains_recursive_call",
        "input": "tests/test_input_contains_recursive_call.cc",
        "preamble": "#include <string>\n#include <vector>\n\nstruct FunctionDecl {\n  std::string Name;\n  FunctionDecl(const std::string &N) : Name(N) {}\n  std::string getNameAsString() const { return Name; }\n};\n\nstruct Stmt {\n  virtual ~Stmt() = default;\n};\n\nstruct StmtRange {\n  static constexpr int Max = 16;\n  Stmt *Items[Max];\n  int Size;\n  StmtRange() : Size(0) {}\n  void push_back(Stmt *S) { Items[Size++] = S; }\n\n  Stmt **begin() { return Items; }\n  Stmt **end() { return Items + Size; }\n  Stmt *const *begin() const { return Items; }\n  Stmt *const *end() const { return Items + Size; }\n\n  struct RevIt {\n    Stmt **P;\n    Stmt *&operator*() { return *(P - 1); }\n    RevIt &operator++() { --P; return *this; }\n    bool operator!=(const RevIt &O) const { return P != O.P; }\n  };\n  struct ConstRevIt {\n    Stmt *const *P;\n    Stmt *const &operator*() { return *(P - 1); }\n    ConstRevIt &operator++() { --P; return *this; }\n    bool operator!=(const ConstRevIt &O) const { return P != O.P; }\n  };\n  RevIt rbegin() { return RevIt{Items + Size}; }\n  RevIt rend() { return RevIt{Items}; }\n  ConstRevIt rbegin() const { return ConstRevIt{Items + Size}; }\n  ConstRevIt rend() const { return ConstRevIt{Items}; }\n};\n\nstruct Expr : Stmt {\n  virtual StmtRange children() const { return StmtRange(); }\n};\n\nstruct CallExpr : Expr {\n  FunctionDecl *Callee;\n  StmtRange Args;\n  CallExpr(FunctionDecl *C) : Callee(C) {}\n  void addArg(Expr *A) { Args.push_back(A); }\n  const FunctionDecl *getDirectCallee() const { return Callee; }\n  StmtRange children() const override {\n    StmtRange R;\n    for (Stmt *S : Args)\n      R.push_back(S);\n    return R;\n  }\n};\n\ntemplate <typename T, typename U>\nconst T *dyn_cast(const U *P) {\n  return dynamic_cast<const T *>(P);\n}\n\ntemplate <typename T, typename U>\nconst T *dyn_cast_or_null(const U *P) {\n  return P ? dynamic_cast<const T *>(P) : nullptr;\n}\n",
        "main": """
#include <iostream>
int main() {
  FunctionDecl target(\"target\"), other(\"other\");
  Expr leaf1, leaf2;
  CallExpr direct(&target); direct.addArg(&leaf1);
  CallExpr nested(&other); nested.addArg(&direct);
  CallExpr otherOnly(&other); otherOnly.addArg(&leaf1);
  std::cout << "direct = " << ContainsRecursiveCall(&direct, \"target\") << std::endl;
  std::cout << "nested = " << ContainsRecursiveCall(&nested, \"target\") << std::endl;
  std::cout << "otherOnly = " << ContainsRecursiveCall(&otherOnly, \"target\") << std::endl;
  std::cout << "null = " << ContainsRecursiveCall(nullptr, \"target\") << std::endl;
  return 0;
}
""",
        "expected": [
            "direct = 1",
            "nested = 1",
            "otherOnly = 0",
            "null = 0",
        ],
    },
    {
        "name": "collect_holes_original",
        "input": "tests/test_input_collect_holes.cc",
        "preamble": "#include <string>\n#include <vector>\n\nstruct FunctionDecl {\n  std::string Name;\n  FunctionDecl(const std::string &N) : Name(N) {}\n  std::string getNameAsString() const { return Name; }\n};\n\nstruct Stmt {\n  virtual ~Stmt() = default;\n};\n\nstruct StmtRange {\n  static constexpr int Max = 16;\n  Stmt *Items[Max];\n  int Size;\n  StmtRange() : Size(0) {}\n  void push_back(Stmt *S) { Items[Size++] = S; }\n\n  Stmt **begin() { return Items; }\n  Stmt **end() { return Items + Size; }\n  Stmt *const *begin() const { return Items; }\n  Stmt *const *end() const { return Items + Size; }\n\n  struct RevIt {\n    Stmt **P;\n    Stmt *&operator*() { return *(P - 1); }\n    RevIt &operator++() { --P; return *this; }\n    bool operator!=(const RevIt &O) const { return P != O.P; }\n  };\n  struct ConstRevIt {\n    Stmt *const *P;\n    Stmt *const &operator*() { return *(P - 1); }\n    ConstRevIt &operator++() { --P; return *this; }\n    bool operator!=(const ConstRevIt &O) const { return P != O.P; }\n  };\n  RevIt rbegin() { return RevIt{Items + Size}; }\n  RevIt rend() { return RevIt{Items}; }\n  ConstRevIt rbegin() const { return ConstRevIt{Items + Size}; }\n  ConstRevIt rend() const { return ConstRevIt{Items}; }\n};\n\nstruct Expr : Stmt {\n  virtual StmtRange children() const { return StmtRange(); }\n};\n\nstruct CallExpr : Expr {\n  FunctionDecl *Callee;\n  StmtRange Args;\n  CallExpr(FunctionDecl *C) : Callee(C) {}\n  void addArg(Expr *A) { Args.push_back(A); }\n  const FunctionDecl *getDirectCallee() const { return Callee; }\n  StmtRange children() const override {\n    StmtRange R;\n    for (Stmt *S : Args)\n      R.push_back(S);\n    return R;\n  }\n};\n\ntemplate <typename T, typename U>\nconst T *dyn_cast(const U *P) {\n  return dynamic_cast<const T *>(P);\n}\n\ntemplate <typename T, typename U>\nconst T *dyn_cast_or_null(const U *P) {\n  return P ? dynamic_cast<const T *>(P) : nullptr;\n}\n",
        "main": """
#include <iostream>
int main() {
  FunctionDecl target(\"target\"), other(\"other\");
  Expr leaf1, leaf2;
  CallExpr inner1(&target); inner1.addArg(&leaf1);
  CallExpr inner2(&target); inner2.addArg(&leaf2);
  CallExpr outer(&other); outer.addArg(&inner1); outer.addArg(&inner2);
  CallExpr otherOnly(&other); otherOnly.addArg(&leaf1);
  std::vector<CallExpr *> holes1, holes2, holes3, holes4;
  CollectHoles(&outer, \"target\", holes1);
  CollectHoles(&inner1, \"target\", holes2);
  CollectHoles(&otherOnly, \"target\", holes3);
  CollectHoles(nullptr, \"target\", holes4);
  std::cout << \"outer_holes = \" << holes1.size() << std::endl;
  std::cout << \"inner_holes = \" << holes2.size() << std::endl;
  std::cout << \"other_holes = \" << holes3.size() << std::endl;
  std::cout << \"null_holes = \" << holes4.size() << std::endl;
  return 0;
}
""",
        "expected": [
            "outer_holes = 2",
            "inner_holes = 1",
            "other_holes = 0",
            "null_holes = 0",
        ],
    },
    {
        "name": "collect_holes_deep_original",
        "input": "tests/test_input_collect_holes_deep.cc",
        "preamble": "#include <string>\n#include <vector>\n\nstruct FunctionDecl {\n  std::string Name;\n  FunctionDecl(const std::string &N) : Name(N) {}\n  std::string getNameAsString() const { return Name; }\n};\n\nstruct Stmt {\n  virtual ~Stmt() = default;\n};\n\nstruct StmtRange {\n  static constexpr int Max = 16;\n  Stmt *Items[Max];\n  int Size;\n  StmtRange() : Size(0) {}\n  void push_back(Stmt *S) { Items[Size++] = S; }\n\n  Stmt **begin() { return Items; }\n  Stmt **end() { return Items + Size; }\n  Stmt *const *begin() const { return Items; }\n  Stmt *const *end() const { return Items + Size; }\n\n  struct RevIt {\n    Stmt **P;\n    Stmt *&operator*() { return *(P - 1); }\n    RevIt &operator++() { --P; return *this; }\n    bool operator!=(const RevIt &O) const { return P != O.P; }\n  };\n  struct ConstRevIt {\n    Stmt *const *P;\n    Stmt *const &operator*() { return *(P - 1); }\n    ConstRevIt &operator++() { --P; return *this; }\n    bool operator!=(const ConstRevIt &O) const { return P != O.P; }\n  };\n  RevIt rbegin() { return RevIt{Items + Size}; }\n  RevIt rend() { return RevIt{Items}; }\n  ConstRevIt rbegin() const { return ConstRevIt{Items + Size}; }\n  ConstRevIt rend() const { return ConstRevIt{Items}; }\n};\n\nstruct Expr : Stmt {\n  virtual StmtRange children() const { return StmtRange(); }\n};\n\nstruct CallExpr : Expr {\n  FunctionDecl *Callee;\n  StmtRange Args;\n  CallExpr(FunctionDecl *C) : Callee(C) {}\n  void addArg(Expr *A) { Args.push_back(A); }\n  const FunctionDecl *getDirectCallee() const { return Callee; }\n  StmtRange children() const override {\n    StmtRange R;\n    for (Stmt *S : Args)\n      R.push_back(S);\n    return R;\n  }\n};\n\ntemplate <typename T, typename U>\nconst T *dyn_cast(const U *P) {\n  return dynamic_cast<const T *>(P);\n}\n\ntemplate <typename T, typename U>\nconst T *dyn_cast_or_null(const U *P) {\n  return P ? dynamic_cast<const T *>(P) : nullptr;\n}\n",
        "main": """
#include <iostream>
int main() {
  FunctionDecl target("target"), other("other");
  Expr leaf1, leaf2;
  CallExpr arg1(&target); arg1.addArg(&leaf1);
  CallExpr arg2(&target); arg2.addArg(&leaf2);
  CallExpr root(&other); root.addArg(&arg1); root.addArg(&arg2);
  std::vector<CallExpr *> holes;
  CollectHolesDeep(&root, "target", holes);
  std::cout << "count = " << holes.size() << std::endl;
  std::cout << "order_ok = " << (holes.size() == 2 && holes[0] == &arg1 && holes[1] == &arg2 ? 1 : 0) << std::endl;
  std::vector<CallExpr *> empty;
  CollectHolesDeep(nullptr, "target", empty);
  std::cout << "null_count = " << empty.size() << std::endl;
  return 0;
}
""",
        "expected": [
            "count = 2",
            "order_ok = 1",
            "null_count = 0",
        ],
    },
    {
        "name": "collect_recursive_calls_original",
        "input": "tests/test_input_collect_recursive_calls.cc",
        "preamble": "#include <string>\n#include <vector>\n\nstruct FunctionDecl {\n  std::string Name;\n  FunctionDecl(const std::string &N) : Name(N) {}\n  std::string getNameAsString() const { return Name; }\n};\n\nstruct StmtRange;\n\nstruct Stmt {\n  virtual ~Stmt() = default;\n  virtual StmtRange children() const;\n};\n\nstruct StmtRange {\n  static constexpr int Max = 16;\n  Stmt *Items[Max];\n  int Size;\n  StmtRange() : Size(0) {}\n  void push_back(Stmt *S) { Items[Size++] = S; }\n\n  Stmt **begin() { return Items; }\n  Stmt **end() { return Items + Size; }\n  Stmt *const *begin() const { return Items; }\n  Stmt *const *end() const { return Items + Size; }\n\n  struct RevIt {\n    Stmt **P;\n    Stmt *&operator*() { return *(P - 1); }\n    RevIt &operator++() { --P; return *this; }\n    bool operator!=(const RevIt &O) const { return P != O.P; }\n  };\n  struct ConstRevIt {\n    Stmt *const *P;\n    Stmt *const &operator*() { return *(P - 1); }\n    ConstRevIt &operator++() { --P; return *this; }\n    bool operator!=(const ConstRevIt &O) const { return P != O.P; }\n  };\n  RevIt rbegin() { return RevIt{Items + Size}; }\n  RevIt rend() { return RevIt{Items}; }\n  ConstRevIt rbegin() const { return ConstRevIt{Items + Size}; }\n  ConstRevIt rend() const { return ConstRevIt{Items}; }\n};\n\nStmtRange Stmt::children() const { return StmtRange(); }\n\nstruct Expr : Stmt {};\n\nstruct CallExpr : Stmt {\n  FunctionDecl *Callee;\n  StmtRange Args;\n  CallExpr(FunctionDecl *C) : Callee(C) {}\n  void addArg(Stmt *A) { Args.push_back(A); }\n  const FunctionDecl *getDirectCallee() const { return Callee; }\n  StmtRange children() const override {\n    StmtRange R;\n    for (Stmt *S : Args)\n      R.push_back(S);\n    return R;\n  }\n};\n\ntemplate <typename T, typename U>\nconst T *dyn_cast(const U *P) {\n  return dynamic_cast<const T *>(P);\n}\n",
        "main": """
#include <iostream>
int main() {
  FunctionDecl target(\"target\"), other(\"other\");
  Expr leaf;
  CallExpr target1(&target); target1.addArg(&leaf);
  CallExpr target2(&target); target2.addArg(&leaf);
  CallExpr root(&other); root.addArg(&leaf); root.addArg(&target1); root.addArg(&target2);
  CallExpr otherOnly(&other); otherOnly.addArg(&leaf);
  std::vector<CallExpr *> calls1, calls2, calls3, calls4;
  CollectRecursiveCallsInStmt(&root, \"target\", calls1);
  CollectRecursiveCallsInStmt(&target1, \"target\", calls2);
  CollectRecursiveCallsInStmt(&otherOnly, \"target\", calls3);
  CollectRecursiveCallsInStmt(nullptr, \"target\", calls4);
  std::cout << \"root_calls = \" << calls1.size() << std::endl;
  std::cout << \"target_calls = \" << calls2.size() << std::endl;
  std::cout << \"other_calls = \" << calls3.size() << std::endl;
  std::cout << \"null_calls = \" << calls4.size() << std::endl;
  return 0;
}
""",
        "expected": [
            "root_calls = 2",
            "target_calls = 1",
            "other_calls = 0",
            "null_calls = 0",
        ],
    },
    {
        "name": "expr_uses_params_original",
        "input": "tests/test_input_expr_uses_params.cc",
        "preamble": "#include <string>\n#include <unordered_set>\n#include <vector>\n\nstruct ValueDecl {\n  std::string Name;\n  ValueDecl(const std::string &N) : Name(N) {}\n  std::string getNameAsString() const { return Name; }\n};\n\nstruct FunctionDecl : ValueDecl {\n  FunctionDecl(const std::string &N) : ValueDecl(N) {}\n};\n\nstruct Stmt {\n  virtual ~Stmt() = default;\n};\n\nstruct StmtRange {\n  static constexpr int Max = 16;\n  Stmt *Items[Max];\n  int Size;\n  StmtRange() : Size(0) {}\n  void push_back(Stmt *S) { Items[Size++] = S; }\n\n  Stmt **begin() { return Items; }\n  Stmt **end() { return Items + Size; }\n  Stmt *const *begin() const { return Items; }\n  Stmt *const *end() const { return Items + Size; }\n\n  struct RevIt {\n    Stmt **P;\n    Stmt *&operator*() { return *(P - 1); }\n    RevIt &operator++() { --P; return *this; }\n    bool operator!=(const RevIt &O) const { return P != O.P; }\n  };\n  struct ConstRevIt {\n    Stmt *const *P;\n    Stmt *const &operator*() { return *(P - 1); }\n    ConstRevIt &operator++() { --P; return *this; }\n    bool operator!=(const ConstRevIt &O) const { return P != O.P; }\n  };\n  RevIt rbegin() { return RevIt{Items + Size}; }\n  RevIt rend() { return RevIt{Items}; }\n  ConstRevIt rbegin() const { return ConstRevIt{Items + Size}; }\n  ConstRevIt rend() const { return ConstRevIt{Items}; }\n};\n\nstruct Expr : Stmt {\n  virtual StmtRange children() const { return StmtRange(); }\n};\n\nstruct DeclRefExpr : Expr {\n  ValueDecl *Decl;\n  DeclRefExpr(ValueDecl *D) : Decl(D) {}\n  const ValueDecl *getDecl() const { return Decl; }\n};\n\nstruct CallExpr : Expr {\n  FunctionDecl *Callee;\n  StmtRange Args;\n  CallExpr(FunctionDecl *C) : Callee(C) {}\n  void addArg(Expr *A) { Args.push_back(A); }\n  const FunctionDecl *getDirectCallee() const { return Callee; }\n  StmtRange children() const override {\n    StmtRange R;\n    for (Stmt *S : Args)\n      R.push_back(S);\n    return R;\n  }\n};\n\ntemplate <typename T, typename U>\nconst T *dyn_cast(const U *P) {\n  return dynamic_cast<const T *>(P);\n}\n\ntemplate <typename T, typename U>\nconst T *dyn_cast_or_null(const U *P) {\n  return P ? dynamic_cast<const T *>(P) : nullptr;\n}\n",
        "main": """
#include <iostream>
int main() {
  ValueDecl x(\"x\"), y(\"y\"), z(\"z\");
  DeclRefExpr refX(&x), refZ(&z);
  CallExpr call(nullptr); call.addArg(&refX); call.addArg(&refZ);
  std::unordered_set<std::string> params;
  params.insert(\"x\");
  params.insert(\"y\");
  std::cout << \"refX = \" << ExprUsesParams(&refX, params) << std::endl;
  std::cout << \"refZ = \" << ExprUsesParams(&refZ, params) << std::endl;
  std::cout << \"call = \" << ExprUsesParams(&call, params) << std::endl;
  std::cout << \"null = \" << ExprUsesParams(nullptr, params) << std::endl;
  return 0;
}
""",
        "expected": [
            "refX = 1",
            "refZ = 0",
            "call = 1",
            "null = 0",
        ],
    },
    {
        "name": "find_recursive_call_return_if_original",
        "input": "tests/test_input_find_recursive_call_return_if.cc",
        "preamble": "#include <string>\n#include <vector>\n\nstruct FunctionDecl {\n  std::string Name;\n  FunctionDecl(const std::string &N) : Name(N) {}\n  std::string getNameAsString() const { return Name; }\n};\n\nstruct Stmt;\n\nstruct StmtRange {\n  static constexpr int Max = 16;\n  Stmt *Items[Max];\n  int Size;\n  StmtRange() : Size(0) {}\n  void push_back(Stmt *S) { Items[Size++] = S; }\n\n  Stmt **begin() { return Items; }\n  Stmt **end() { return Items + Size; }\n  Stmt *const *begin() const { return Items; }\n  Stmt *const *end() const { return Items + Size; }\n\n  struct RevIt {\n    Stmt **P;\n    Stmt *&operator*() { return *(P - 1); }\n    RevIt &operator++() { --P; return *this; }\n    bool operator!=(const RevIt &O) const { return P != O.P; }\n  };\n  struct ConstRevIt {\n    Stmt *const *P;\n    Stmt *const &operator*() { return *(P - 1); }\n    ConstRevIt &operator++() { --P; return *this; }\n    bool operator!=(const ConstRevIt &O) const { return P != O.P; }\n  };\n  RevIt rbegin() { return RevIt{Items + Size}; }\n  RevIt rend() { return RevIt{Items}; }\n  ConstRevIt rbegin() const { return ConstRevIt{Items + Size}; }\n  ConstRevIt rend() const { return ConstRevIt{Items}; }\n};\n\nstruct Stmt {\n  virtual ~Stmt() = default;\n  virtual StmtRange children() const { return StmtRange(); }\n};\n\nstruct Expr : Stmt {\n  virtual const Expr *IgnoreParenImpCasts() const { return this; }\n};\n\nstruct CallExpr : Expr {\n  FunctionDecl *Callee;\n  StmtRange Args;\n  CallExpr(FunctionDecl *C) : Callee(C) {}\n  void addArg(Expr *A) { Args.push_back(A); }\n  const FunctionDecl *getDirectCallee() const { return Callee; }\n  StmtRange children() const override {\n    StmtRange R;\n    for (Stmt *S : Args)\n      R.push_back(S);\n    return R;\n  }\n};\n\nstruct IfStmt : Stmt {\n  const Expr *Cond;\n  const Stmt *Then;\n  IfStmt(const Expr *C, const Stmt *T) : Cond(C), Then(T) {}\n  const Expr *getCond() const { return Cond; }\n  const Stmt *getThen() const { return Then; }\n  StmtRange children() const override {\n    StmtRange R;\n    R.push_back(const_cast<Stmt *>(Then));\n    return R;\n  }\n};\n\nstruct ReturnStmt : Stmt {};\n\nstruct Block : Stmt {\n  StmtRange Children;\n  void addChild(Stmt *S) { Children.push_back(S); }\n  StmtRange children() const override { return Children; }\n};\n\ntemplate <typename T, typename U>\nconst T *dyn_cast(const U *P) {\n  return dynamic_cast<const T *>(P);\n}\n\ntemplate <typename T, typename U>\nconst T *dyn_cast_or_null(const U *P) {\n  return P ? dynamic_cast<const T *>(P) : nullptr;\n}\n",
        "main": """
#include <iostream>
int main() {
  FunctionDecl target(\"target\"), other(\"other\");
  ReturnStmt rs;
  CallExpr targetCall(&target);
  IfStmt targetIf(&targetCall, &rs);
  CallExpr otherCall(&other);
  IfStmt otherIf(&otherCall, &rs);
  Block root; root.addChild(&otherIf); root.addChild(&targetIf);
  CallExpr *out = nullptr;
  std::cout << \"found = \" << (FindRecursiveCallReturnIf(&root, \"target\", out) != nullptr) << std::endl;
  std::cout << \"out_set = \" << (out != nullptr) << std::endl;
  std::cout << \"direct = \" << (FindRecursiveCallReturnIf(&targetIf, \"target\", out) != nullptr) << std::endl;
  std::cout << \"miss = \" << (FindRecursiveCallReturnIf(&otherIf, \"target\", out) != nullptr) << std::endl;
  std::cout << \"null = \" << (FindRecursiveCallReturnIf(nullptr, \"target\", out) != nullptr) << std::endl;
  return 0;
}
""",
        "expected": [
            "found = 1",
            "out_set = 1",
            "direct = 1",
            "miss = 0",
            "null = 0",
        ],
    },
    {
        "name": "contains_non_recursive_call_original",
        "input": "tests/test_input_contains_non_recursive_call.cc",
        "preamble": "#include <string>\n#include <vector>\n\nstruct FunctionDecl {\n  std::string Name;\n  FunctionDecl(const std::string &N) : Name(N) {}\n  std::string getNameAsString() const { return Name; }\n};\n\nstruct Stmt;\n\nstruct StmtRange {\n  static constexpr int Max = 16;\n  Stmt *Items[Max];\n  int Size;\n  StmtRange() : Size(0) {}\n  void push_back(Stmt *S) { Items[Size++] = S; }\n\n  Stmt **begin() { return Items; }\n  Stmt **end() { return Items + Size; }\n  Stmt *const *begin() const { return Items; }\n  Stmt *const *end() const { return Items + Size; }\n\n  struct RevIt {\n    Stmt **P;\n    Stmt *&operator*() { return *(P - 1); }\n    RevIt &operator++() { --P; return *this; }\n    bool operator!=(const RevIt &O) const { return P != O.P; }\n  };\n  struct ConstRevIt {\n    Stmt *const *P;\n    Stmt *const &operator*() { return *(P - 1); }\n    ConstRevIt &operator++() { --P; return *this; }\n    bool operator!=(const ConstRevIt &O) const { return P != O.P; }\n  };\n  RevIt rbegin() { return RevIt{Items + Size}; }\n  RevIt rend() { return RevIt{Items}; }\n  ConstRevIt rbegin() const { return ConstRevIt{Items + Size}; }\n  ConstRevIt rend() const { return ConstRevIt{Items}; }\n};\n\nstruct Stmt {\n  virtual ~Stmt() = default;\n  virtual StmtRange children() const { return StmtRange(); }\n};\n\nstruct Expr : Stmt {};\n\nstruct CallExpr : Expr {\n  FunctionDecl *Callee;\n  StmtRange Args;\n  CallExpr(FunctionDecl *C) : Callee(C) {}\n  void addArg(Expr *A) { Args.push_back(A); }\n  const FunctionDecl *getDirectCallee() const { return Callee; }\n  StmtRange children() const override {\n    StmtRange R;\n    for (Stmt *S : Args)\n      R.push_back(S);\n    return R;\n  }\n};\n\ntemplate <typename T, typename U>\nconst T *dyn_cast(const U *P) {\n  return dynamic_cast<const T *>(P);\n}\n\ntemplate <typename T, typename U>\nconst T *dyn_cast_or_null(const U *P) {\n  return P ? dynamic_cast<const T *>(P) : nullptr;\n}\n",
        "main": """
#include <iostream>
int main() {
  FunctionDecl target(\"target\"), other(\"other\");
  Expr leaf;
  CallExpr targetCall(&target); targetCall.addArg(&leaf);
  CallExpr targetOnly(&target); targetOnly.addArg(&leaf);
  CallExpr root(&other); root.addArg(&targetCall);
  std::cout << \"root = \" << ContainsNonRecursiveCall(&root, \"target\") << std::endl;
  std::cout << \"targetCall = \" << ContainsNonRecursiveCall(&targetCall, \"target\") << std::endl;
  std::cout << \"targetOnly = \" << ContainsNonRecursiveCall(&targetOnly, \"target\") << std::endl;
  std::cout << \"null = \" << ContainsNonRecursiveCall(nullptr, \"target\") << std::endl;
  return 0;
}
""",
        "expected": [
            "root = 1",
            "targetCall = 0",
            "targetOnly = 0",
            "null = 0",
        ],
    },
    {
        "name": "tree_holes",
        "input": "tests/test_input_tree_holes.cc",
        "preamble": "struct TreeNode;\n\nstruct NodeList {\n  TreeNode *data[10];\n  int size;\n  NodeList() : size(0) {}\n  void push_back(TreeNode *x) { data[size++] = x; }\n  TreeNode **begin() { return data; }\n  TreeNode **end() { return data + size; }\n\n  struct RevIt {\n    TreeNode **p;\n    TreeNode *&operator*() { return *(p - 1); }\n    RevIt &operator++() { --p; return *this; }\n    bool operator!=(const RevIt &o) const { return p != o.p; }\n  };\n  RevIt rbegin() { return RevIt{data + size}; }\n  RevIt rend() { return RevIt{data}; }\n\n  struct CRevIt {\n    TreeNode *const *p;\n    TreeNode *const &operator*() { return *(p - 1); }\n    CRevIt &operator++() { --p; return *this; }\n    bool operator!=(const CRevIt &o) const { return p != o.p; }\n  };\n  CRevIt rbegin() const { return CRevIt{data + size}; }\n  CRevIt rend() const { return CRevIt{data}; }\n};\n\nstruct IntList {\n  int data[100];\n  int size;\n  IntList() : size(0) {}\n  void push_back(int x) { data[size++] = x; }\n  int operator[](int i) const { return data[i]; }\n  int get_size() const { return size; }\n};\n\nstruct TreeNode {\n  int value;\n  NodeList children;\n};\n",
        "main": """
#include <iostream>
int main() {
  TreeNode n4{4, {}};
  TreeNode n5{5, {}};
  TreeNode n2{2, {}};
  n2.children.push_back(&n4);
  n2.children.push_back(&n5);
  TreeNode n3{3, {}};
  TreeNode root{1, {}};
  root.children.push_back(&n2);
  root.children.push_back(&n3);
  IntList holes;
  collect_holes(&root, holes);
  for (int i = 0; i < holes.get_size(); ++i) {
    std::cout << "hole[" << i << "] = " << holes[i] << std::endl;
  }
  return 0;
}
""",
        "expected": [
            "hole[0] = 1",
            "hole[1] = 2",
            "hole[2] = 4",
            "hole[3] = 5",
            "hole[4] = 3",
        ],
    },
    {
        "name": "is_in_tail_position_original",
        "input": "tests/test_input_is_in_tail_position.cc",
        "preamble": """
#include <string>
#include <vector>
#include <iostream>

enum Opcode { BO_LAnd, BO_LOr };

struct Stmt { virtual ~Stmt() = default; };
struct Expr : Stmt { virtual const Expr *IgnoreParenImpCasts() const { return this; } };

struct FunctionDecl {
  std::string Name;
  FunctionDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
};

struct ReturnStmt : Stmt {
  const Expr *Ret;
  ReturnStmt(const Expr *R) : Ret(R) {}
  const Expr *getRetValue() const { return Ret; }
};

struct IfStmt : Stmt {
  const Expr *Cond;
  const Stmt *Then_;
  const Stmt *Else_;
  IfStmt(const Expr *C, const Stmt *T, const Stmt *E = nullptr)
      : Cond(C), Then_(T), Else_(E) {}
  const Expr *getCond() const { return Cond; }
  const Stmt *getThen() const { return Then_; }
  const Stmt *getElse() const { return Else_; }
};

struct CompoundStmt : Stmt {
  std::vector<const Stmt *> Body;
  void addChild(const Stmt *S) { Body.push_back(S); }
  bool body_empty() const { return Body.empty(); }
  const Stmt *body_back() const { return Body.back(); }
};

struct BinaryOperator : Expr {
  Opcode Op;
  Expr *LHS;
  Expr *RHS;
  BinaryOperator(Opcode O, Expr *L, Expr *R) : Op(O), LHS(L), RHS(R) {}
  Opcode getOpcode() const { return Op; }
  const Expr *getLHS() const { return LHS; }
  const Expr *getRHS() const { return RHS; }
};

struct CallExpr : Expr {
  FunctionDecl *Callee;
  std::vector<Expr *> Args;
  CallExpr(FunctionDecl *C) : Callee(C) {}
  void addArg(Expr *A) { Args.push_back(A); }
  const FunctionDecl *getDirectCallee() const { return Callee; }
  unsigned getNumArgs() const { return static_cast<unsigned>(Args.size()); }
  const Expr *getArg(unsigned I) const { return Args[I]; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }
""",
        "main": """
#include <iostream>
int main() {
  FunctionDecl target("target");
  FunctionDecl other("other");
  Expr cond;
  BinaryOperator land(BO_LAnd, &cond, &cond);

  CallExpr targetCall(&target);
  ReturnStmt rs1(&targetCall);
  std::cout << "tail_target = " << IsInTailPosition(&rs1, &target) << std::endl;

  CallExpr otherCall(&other);
  ReturnStmt rs2(&otherCall);
  std::cout << "tail_other = " << IsInTailPosition(&rs2, &target) << std::endl;

  CallExpr tc2(&target);
  ReturnStmt rs_then(&tc2), rs_else(&tc2);
  IfStmt if1(&land, &rs_then, &rs_else);
  std::cout << "tail_if_both = " << IsInTailPosition(&if1, &target) << std::endl;

  ReturnStmt rs_then2(&otherCall);
  IfStmt if2(&land, &rs_then2, &rs_else);
  std::cout << "tail_if_mixed = " << IsInTailPosition(&if2, &target) << std::endl;

  CompoundStmt cs;
  cs.addChild(&rs2);
  cs.addChild(&rs1);
  std::cout << "tail_compound = " << IsInTailPosition(&cs, &target) << std::endl;

  std::cout << "tail_null = " << IsInTailPosition(nullptr, &target) << std::endl;

  ReturnStmt rs_empty(nullptr);
  std::cout << "tail_empty_return = " << IsInTailPosition(&rs_empty, &target) << std::endl;

  return 0;
}
""",
        "expected": [
            "tail_target = 1",
            "tail_other = 0",
            "tail_if_both = 1",
            "tail_if_mixed = 0",
            "tail_compound = 1",
            "tail_null = 0",
            "tail_empty_return = 0",
        ],
    },
    {
        "name": "eval_condition_original",
        "input": "tests/test_input_eval_condition.cc",
        "preamble": """
#include <string>
#include <vector>
#include <iostream>

enum Opcode { BO_LAnd, BO_LOr, BO_EQ, BO_NE, BO_LT, BO_GT, BO_LE, BO_GE, BO_Add };
enum UOpcode { UO_LNot };

struct Stmt { virtual ~Stmt() = default; };
struct Expr : Stmt { virtual const Expr *IgnoreParenImpCasts() const { return this; } };

struct ValueDecl {
  std::string Name;
  ValueDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
};

struct DeclRefExpr : Expr {
  const ValueDecl *Decl_;
  DeclRefExpr(const ValueDecl *D) : Decl_(D) {}
  const ValueDecl *getDecl() const { return Decl_; }
};

struct APInt {
  long long V;
  APInt(long long v) : V(v) {}
  long long getSExtValue() const { return V; }
};

struct IntegerLiteral : Expr {
  APInt V;
  IntegerLiteral(long long v) : V(v) {}
  const APInt &getValue() const { return V; }
};

struct BinaryOperator : Expr {
  Opcode Op;
  Expr *LHS;
  Expr *RHS;
  BinaryOperator(Opcode O, Expr *L, Expr *R) : Op(O), LHS(L), RHS(R) {}
  Opcode getOpcode() const { return Op; }
  const Expr *getLHS() const { return LHS; }
  const Expr *getRHS() const { return RHS; }
};

struct UnaryOperator : Expr {
  UOpcode Op;
  Expr *Sub;
  UnaryOperator(UOpcode O, Expr *S) : Op(O), Sub(S) {}
  UOpcode getOpcode() const { return Op; }
  const Expr *getSubExpr() const { return Sub; }
};

enum class EvalResult { True, False, Unknown };

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

bool ExtractParamOrLiteral(const Expr *E, const std::string &ParamName,
                           int ParamValue, int &Out) {
  E = E->IgnoreParenImpCasts();
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (DRE->getDecl()->getNameAsString() == ParamName) {
      Out = ParamValue;
      return true;
    }
  }
  if (const IntegerLiteral *IL = dyn_cast<IntegerLiteral>(E)) {
    Out = static_cast<int>(IL->getValue().getSExtValue());
    return true;
  }
  return false;
}
""",
        "main": """
#include <iostream>
int val(const EvalResult &r) {
  return r == EvalResult::True ? 1 : (r == EvalResult::False ? 0 : -1);
}
int main() {
  std::string param = "n";
  ValueDecl nDecl("n");
  IntegerLiteral five(5), three(3);
  DeclRefExpr nRef(&nDecl);

  BinaryOperator eq(BO_EQ, &nRef, &five);
  BinaryOperator ne(BO_NE, &nRef, &five);
  BinaryOperator lt(BO_LT, &nRef, &five);
  BinaryOperator gt(BO_GT, &nRef, &three);
  BinaryOperator le(BO_LE, &nRef, &five);
  BinaryOperator ge(BO_GE, &nRef, &five);
  BinaryOperator land(BO_LAnd, &eq, &gt);
  BinaryOperator lor(BO_LOr, &ne, &lt);
  UnaryOperator lnot(UO_LNot, &eq);
  BinaryOperator add(BO_Add, &nRef, &five);

  std::cout << "eq=" << val(EvalConditionForParam(&eq, param, 5)) << std::endl;
  std::cout << "ne=" << val(EvalConditionForParam(&ne, param, 5)) << std::endl;
  std::cout << "lt=" << val(EvalConditionForParam(&lt, param, 5)) << std::endl;
  std::cout << "gt=" << val(EvalConditionForParam(&gt, param, 5)) << std::endl;
  std::cout << "le=" << val(EvalConditionForParam(&le, param, 5)) << std::endl;
  std::cout << "ge=" << val(EvalConditionForParam(&ge, param, 5)) << std::endl;
  std::cout << "land=" << val(EvalConditionForParam(&land, param, 5)) << std::endl;
  std::cout << "lor=" << val(EvalConditionForParam(&lor, param, 5)) << std::endl;
  std::cout << "lnot=" << val(EvalConditionForParam(&lnot, param, 5)) << std::endl;
  std::cout << "unknown=" << val(EvalConditionForParam(&add, param, 5)) << std::endl;
  return 0;
}
""",
        "expected": [
            "eq=1",
            "ne=0",
            "lt=0",
            "gt=1",
            "le=1",
            "ge=1",
            "land=1",
            "lor=0",
            "lnot=0",
            "unknown=-1",
        ],
    },
    {
        "name": "parse_linear_terms_original",
        "input": "tests/test_input_parse_linear_terms.cc",
        "preamble": """
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

enum Opcode { BO_Add, BO_Sub };
enum UOpcode { UO_Minus };

struct Stmt { virtual ~Stmt() = default; };
struct Expr : Stmt { virtual const Expr *IgnoreParenImpCasts() const { return this; } };

struct ValueDecl {
  std::string Name;
  ValueDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
};
struct FunctionDecl : ValueDecl {
  FunctionDecl(const std::string &N) : ValueDecl(N) {}
};

struct DeclRefExpr : Expr {
  const ValueDecl *Decl_;
  DeclRefExpr(const ValueDecl *D) : Decl_(D) {}
  const ValueDecl *getDecl() const { return Decl_; }
};

struct APInt {
  long long V;
  APInt(long long v) : V(v) {}
  long long getSExtValue() const { return V; }
};
struct IntegerLiteral : Expr {
  APInt V;
  IntegerLiteral(long long v) : V(v) {}
  const APInt &getValue() const { return V; }
};

struct BinaryOperator : Expr {
  Opcode Op;
  Expr *LHS;
  Expr *RHS;
  BinaryOperator(Opcode O, Expr *L, Expr *R) : Op(O), LHS(L), RHS(R) {}
  Opcode getOpcode() const { return Op; }
  const Expr *getLHS() const { return LHS; }
  const Expr *getRHS() const { return RHS; }
};

struct UnaryOperator : Expr {
  UOpcode Op;
  Expr *Sub;
  UnaryOperator(UOpcode O, Expr *S) : Op(O), Sub(S) {}
  UOpcode getOpcode() const { return Op; }
  const Expr *getSubExpr() const { return Sub; }
};

struct CallExpr : Expr {
  FunctionDecl *Callee;
  std::vector<Expr *> Args;
  CallExpr(FunctionDecl *C) : Callee(C) {}
  void addArg(Expr *A) { Args.push_back(A); }
  const FunctionDecl *getDirectCallee() const { return Callee; }
  unsigned getNumArgs() const { return static_cast<unsigned>(Args.size()); }
  const Expr *getArg(unsigned I) const { return Args[I]; }
};

struct LinearTerm { int Order; int Sign; CallExpr *Hole; };

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }
""",
        "main": """
#include <iostream>
int main() {
  std::string func = "f";
  std::string param = "n";
  ValueDecl nDecl("n");
  DeclRefExpr nRef(&nDecl);
  FunctionDecl fDecl("f");

  auto makeF = [&](int k) -> CallExpr * {
    IntegerLiteral *ik = new IntegerLiteral(k);
    BinaryOperator *sub = new BinaryOperator(BO_Sub, &nRef, ik);
    CallExpr *call = new CallExpr(&fDecl);
    call->addArg(sub);
    return call;
  };

  CallExpr *f1 = makeF(1);
  std::vector<LinearTerm> terms;
  int maxOrder = 0;
  bool ok = ParseLinearTerms(f1, func, param, terms, maxOrder);
  std::cout << "single_ok=" << ok << " max=" << maxOrder << " terms=" << terms.size() << std::endl;

  CallExpr *f2 = makeF(2);
  CallExpr *f3 = makeF(3);
  BinaryOperator *sub1 = new BinaryOperator(BO_Sub, f1, f2);
  BinaryOperator *expr = new BinaryOperator(BO_Add, sub1, f3);
  terms.clear(); maxOrder = 0;
  ok = ParseLinearTerms(expr, func, param, terms, maxOrder);
  std::cout << "expr_ok=" << ok << " max=" << maxOrder;
  for (size_t i = 0; i < terms.size(); ++i)
    std::cout << " [" << terms[i].Order << "," << terms[i].Sign << "]";
  std::cout << std::endl;

  BinaryOperator *inner = new BinaryOperator(BO_Sub, f1, f2);
  UnaryOperator *neg = new UnaryOperator(UO_Minus, inner);
  terms.clear(); maxOrder = 0;
  ok = ParseLinearTerms(neg, func, param, terms, maxOrder);
  std::cout << "neg_ok=" << ok << " max=" << maxOrder;
  for (size_t i = 0; i < terms.size(); ++i)
    std::cout << " [" << terms[i].Order << "," << terms[i].Sign << "]";
  std::cout << std::endl;

  return 0;
}
""",
        "expected": [
            "single_ok=1 max=1 terms=1",
            "expr_ok=1 max=3 [1,1] [2,-1] [3,1]",
            "neg_ok=1 max=2 [1,-1] [2,1]",
        ],
    },
    {
        "name": "is_in_tail_position_expr_original",
        "input": "tests/test_input_is_in_tail_position_expr.cc",
        "preamble": """
#include <string>

struct Stmt { virtual ~Stmt() = default; };
struct Expr : Stmt {};

struct ReturnStmt : Stmt {
  const Expr *Ret;
  ReturnStmt(const Expr *R) : Ret(R) {}
  const Expr *getRetValue() const { return Ret; }
};

struct IfStmt : Stmt {
  const Expr *Cond;
  const Stmt *Then_;
  const Stmt *Else_;
  IfStmt(const Expr *C, const Stmt *T, const Stmt *E = nullptr)
      : Cond(C), Then_(T), Else_(E) {}
  const Stmt *getThen() const { return Then_; }
  const Stmt *getElse() const { return Else_; }
};

struct StmtRange {
  const Stmt *const *Begin;
  const Stmt *const *End;
  const Stmt *const *begin() const { return Begin; }
  const Stmt *const *end() const { return End; }
};

struct CompoundStmt : Stmt {
  static constexpr int Max = 16;
  const Stmt *Items[Max];
  int Size;
  CompoundStmt() : Size(0) {}
  void addChild(const Stmt *S) { Items[Size++] = S; }
  bool body_empty() const { return Size == 0; }
  StmtRange body() const { return StmtRange{Items, Items + Size}; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }
""",
        "main": """
#include <iostream>
int main() {
  Expr target;
  Expr other;
  ReturnStmt rs_target(&target);
  ReturnStmt rs_other(&other);
  std::cout << "tail_return_target = " << IsInTailPosition(&target, &rs_target, "f") << std::endl;
  std::cout << "tail_return_other = " << IsInTailPosition(&target, &rs_other, "f") << std::endl;
  IfStmt if_both(nullptr, &rs_target, &rs_target);
  std::cout << "tail_if_both = " << IsInTailPosition(&target, &if_both, "f") << std::endl;
  IfStmt if_mixed(nullptr, &rs_other, &rs_target);
  std::cout << "tail_if_mixed = " << IsInTailPosition(&target, &if_mixed, "f") << std::endl;
  IfStmt if_none(nullptr, &rs_other, &rs_other);
  std::cout << "tail_if_none = " << IsInTailPosition(&target, &if_none, "f") << std::endl;
  CompoundStmt cs;
  cs.addChild(&rs_other);
  cs.addChild(&rs_target);
  std::cout << "tail_compound = " << IsInTailPosition(&target, &cs, "f") << std::endl;
  CompoundStmt cs2;
  cs2.addChild(&rs_target);
  cs2.addChild(&rs_other);
  std::cout << "tail_compound2 = " << IsInTailPosition(&target, &cs2, "f") << std::endl;
  std::cout << "tail_null = " << IsInTailPosition(nullptr, &rs_target, "f") << std::endl;
  std::cout << "tail_expr = " << IsInTailPosition(&target, &target, "f") << std::endl;
  return 0;
}
""",
        "expected": [
            "tail_return_target = 1",
            "tail_return_other = 0",
            "tail_if_both = 1",
            "tail_if_mixed = 1",
            "tail_if_none = 0",
            "tail_compound = 1",
            "tail_compound2 = 0",
            "tail_null = 0",
            "tail_expr = 1",
        ],
    },
    {
        "name": "is_pure_expr_ignore_original",
        "input": "tests/test_input_is_pure_expr_ignore.cc",
        "preamble": """
#include <string>

bool IsKnownPureFunction(const std::string &Name) {
  return Name == "min" || Name == "max" || Name == "std::min" ||
         Name == "std::max" || Name == "pure";
}

struct FunctionDecl {
  std::string Name;
  bool IsConst;
  FunctionDecl(const std::string &N, bool C = false) : Name(N), IsConst(C) {}
  std::string getNameAsString() const { return Name; }
  bool isConst() const { return IsConst; }
};

struct StmtRange;

struct Stmt {
  virtual ~Stmt() = default;
  virtual StmtRange children() const;
};

enum Opcode {
  BO_Add,
  BO_Sub,
  BO_Comma,
  BO_Assign
};

enum UOpcode {
  UO_LNot,
  UO_PreInc,
  UO_PreDec,
  UO_PostInc,
  UO_PostDec
};

struct StmtRange {
  static constexpr int Max = 16;
  Stmt *Items[Max];
  int Size;
  StmtRange() : Size(0) {}
  void push_back(Stmt *S) { Items[Size++] = S; }

  Stmt **begin() { return Items; }
  Stmt **end() { return Items + Size; }
  Stmt *const *begin() const { return Items; }
  Stmt *const *end() const { return Items + Size; }

  struct RevIt {
    Stmt **P;
    Stmt *&operator*() { return *(P - 1); }
    RevIt &operator++() { --P; return *this; }
    bool operator!=(const RevIt &O) const { return P != O.P; }
  };
  struct ConstRevIt {
    Stmt *const *P;
    Stmt *const &operator*() { return *(P - 1); }
    ConstRevIt &operator++() { --P; return *this; }
    bool operator!=(const ConstRevIt &O) const { return P != O.P; }
  };
  RevIt rbegin() { return RevIt{Items + Size}; }
  RevIt rend() { return RevIt{Items}; }
  ConstRevIt rbegin() const { return ConstRevIt{Items + Size}; }
  ConstRevIt rend() const { return ConstRevIt{Items}; }
};

StmtRange Stmt::children() const { return StmtRange(); }

struct Expr : Stmt {
  virtual const Expr *IgnoreParenImpCasts() const { return this; }
  virtual StmtRange children() const override { return StmtRange(); }
};

struct CallExpr : Expr {
  FunctionDecl *Callee;
  StmtRange Args;
  CallExpr(FunctionDecl *C) : Callee(C) {}
  void addArg(Expr *A) { Args.push_back(A); }
  const FunctionDecl *getDirectCallee() const { return Callee; }
  unsigned getNumArgs() const { return static_cast<unsigned>(Args.Size); }
  Expr *getArg(unsigned i) const { return static_cast<Expr *>(Args.Items[i]); }
  StmtRange children() const override {
    StmtRange R;
    for (Stmt *S : Args)
      R.push_back(S);
    return R;
  }
};

struct BinaryOperator : Expr {
  Opcode Op;
  Expr *LHS;
  Expr *RHS;
  BinaryOperator(Opcode O, Expr *L, Expr *R) : Op(O), LHS(L), RHS(R) {}
  Opcode getOpcode() const { return Op; }
  bool isAssignmentOp() const { return Op == BO_Assign; }
  const Expr *getLHS() const { return LHS; }
  const Expr *getRHS() const { return RHS; }
  StmtRange children() const override {
    StmtRange R;
    R.push_back(LHS);
    R.push_back(RHS);
    return R;
  }
};

struct UnaryOperator : Expr {
  UOpcode Op;
  Expr *Sub;
  UnaryOperator(UOpcode O, Expr *S) : Op(O), Sub(S) {}
  UOpcode getOpcode() const { return Op; }
  bool isIncrementDecrementOp() const {
    return Op == UO_PreInc || Op == UO_PreDec ||
           Op == UO_PostInc || Op == UO_PostDec;
  }
  const Expr *getSubExpr() const { return Sub; }
  StmtRange children() const override {
    StmtRange R;
    R.push_back(Sub);
    return R;
  }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) {
  return dynamic_cast<const T *>(P);
}

template <typename T, typename U>
const T *dyn_cast_or_null(const U *P) {
  return P ? dynamic_cast<const T *>(P) : nullptr;
}
""",
        "main": """
#include <iostream>
int main() {
  FunctionDecl pureFn("pure", true);
  FunctionDecl impureFn("impure", false);
  FunctionDecl targetFn("f", false);
  Expr leaf;
  CallExpr targetCall(&targetFn);
  CallExpr impureCall(&impureFn);
  impureCall.addArg(&leaf);
  CallExpr pureCall(&pureFn);
  pureCall.addArg(&leaf);
  CallExpr pureCallWithImpureArg(&pureFn);
  pureCallWithImpureArg.addArg(&impureCall);
  BinaryOperator assign(BO_Assign, &leaf, &leaf);
  BinaryOperator comma(BO_Comma, &leaf, &leaf);
  BinaryOperator add(BO_Add, &leaf, &leaf);
  UnaryOperator inc(UO_PreInc, &leaf);
  std::cout << "leaf = " << IsPureExprIgnoringRecursiveCallsImpl(&leaf, "f") << std::endl;
  std::cout << "targetCall = " << IsPureExprIgnoringRecursiveCallsImpl(&targetCall, "f") << std::endl;
  std::cout << "impureCall = " << IsPureExprIgnoringRecursiveCallsImpl(&impureCall, "f") << std::endl;
  std::cout << "pureCall = " << IsPureExprIgnoringRecursiveCallsImpl(&pureCall, "f") << std::endl;
  std::cout << "pureCallWithImpureArg = " << IsPureExprIgnoringRecursiveCallsImpl(&pureCallWithImpureArg, "f") << std::endl;
  std::cout << "assign = " << IsPureExprIgnoringRecursiveCallsImpl(&assign, "f") << std::endl;
  std::cout << "comma = " << IsPureExprIgnoringRecursiveCallsImpl(&comma, "f") << std::endl;
  std::cout << "add = " << IsPureExprIgnoringRecursiveCallsImpl(&add, "f") << std::endl;
  std::cout << "inc = " << IsPureExprIgnoringRecursiveCallsImpl(&inc, "f") << std::endl;
  return 0;
}
""",
        "expected": [
            "leaf = 1",
            "targetCall = 1",
            "impureCall = 0",
            "pureCall = 1",
            "pureCallWithImpureArg = 0",
            "assign = 0",
            "comma = 0",
            "add = 1",
            "inc = 0",
        ],
    },
    {
        "name": "is_return_or_if_return_or_switch_original",
        "input": "tests/test_input_is_return_or_if_return_or_switch.cc",
        "preamble": """
struct Stmt { virtual ~Stmt() = default; };
struct ReturnStmt : Stmt {};
struct IfStmt : Stmt {};
struct SwitchStmt : Stmt {};
struct CompoundStmt : Stmt {
  static constexpr int Max = 8;
  const Stmt *Items[Max];
  int Size;
  CompoundStmt() : Size(0) {}
  void addChild(const Stmt *S) { Items[Size++] = S; }
  int size() const { return Size; }
  const Stmt *const *body_begin() const { return Items; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

template <typename T, typename U>
bool isa(const U *P) { return dyn_cast<T>(P) != nullptr; }
""",
        "main": """
#include <iostream>
int main() {
  ReturnStmt rs;
  IfStmt is;
  SwitchStmt ss;
  CompoundStmt cs_return;
  cs_return.addChild(&rs);
  CompoundStmt cs_if;
  cs_if.addChild(&is);
  CompoundStmt cs_two;
  cs_two.addChild(&rs);
  cs_two.addChild(&is);
  CompoundStmt cs_nested;
  cs_nested.addChild(&cs_return);
  Stmt other;
  std::cout << "return = " << IsReturnOrIfReturnOrSwitch(&rs) << std::endl;
  std::cout << "if = " << IsReturnOrIfReturnOrSwitch(&is) << std::endl;
  std::cout << "switch = " << IsReturnOrIfReturnOrSwitch(&ss) << std::endl;
  std::cout << "compound_return = " << IsReturnOrIfReturnOrSwitch(&cs_return) << std::endl;
  std::cout << "compound_if = " << IsReturnOrIfReturnOrSwitch(&cs_if) << std::endl;
  std::cout << "compound_two = " << IsReturnOrIfReturnOrSwitch(&cs_two) << std::endl;
  std::cout << "compound_nested = " << IsReturnOrIfReturnOrSwitch(&cs_nested) << std::endl;
  std::cout << "other = " << IsReturnOrIfReturnOrSwitch(&other) << std::endl;
  return 0;
}
""",
        "expected": [
            "return = 1",
            "if = 1",
            "switch = 1",
            "compound_return = 1",
            "compound_if = 1",
            "compound_two = 0",
            "compound_nested = 1",
            "other = 0",
        ],
    },
    {
        "name": "collect_callees_original",
        "input": "tests/test_input_collect_callees.cc",
        "preamble": """
#include <string>

namespace std {

template <typename T>
class unordered_set {
public:
  static constexpr int Max = 16;
  T Items[Max];
  int Size;
  unordered_set() : Size(0) {}
  void insert(const T &V) {
    for (int i = 0; i < Size; ++i)
      if (Items[i] == V)
        return;
    Items[Size++] = V;
  }
};

} // namespace std

struct StmtRange;

struct Stmt {
  virtual ~Stmt() = default;
  virtual StmtRange children() const;
};

struct StmtRange {
  static constexpr int Max = 16;
  const Stmt *Items[Max];
  int Size;
  StmtRange() : Size(0) {}
  void push_back(const Stmt *S) { Items[Size++] = S; }
  const Stmt *const *begin() const { return Items; }
  const Stmt *const *end() const { return Items + Size; }

  struct RevIt {
    const Stmt *const *P;
    const Stmt *operator*() { return *(P - 1); }
    RevIt &operator++() { --P; return *this; }
    bool operator!=(const RevIt &O) const { return P != O.P; }
  };
  RevIt rbegin() { return RevIt{Items + Size}; }
  RevIt rend() { return RevIt{Items}; }
};

StmtRange Stmt::children() const { return StmtRange(); }

struct Expr : Stmt {
  StmtRange children() const override { return StmtRange(); }
};

struct FunctionDecl {
  std::string Name;
  FunctionDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
};

struct CallExpr : Expr {
  FunctionDecl *Callee;
  StmtRange Args;
  CallExpr(FunctionDecl *C) : Callee(C) {}
  void addArg(Expr *A) { Args.push_back(A); }
  FunctionDecl *getDirectCallee() const { return Callee; }
  StmtRange children() const override {
    StmtRange R;
    for (const Stmt *S : Args)
      R.push_back(S);
    return R;
  }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }
""",
        "main": """
#include <iostream>
int main() {
  FunctionDecl f("foo"), g("bar");
  Expr leaf;
  CallExpr call_f(&f);
  call_f.addArg(&leaf);
  CallExpr call_g(&g);
  call_g.addArg(&call_f);
  std::unordered_set<std::string> callees;
  CollectCallees(&call_g, callees);
  std::cout << "count = " << callees.Size << std::endl;
  bool has_foo = false, has_bar = false;
  for (int i = 0; i < callees.Size; ++i) {
    if (callees.Items[i] == "foo") has_foo = true;
    if (callees.Items[i] == "bar") has_bar = true;
  }
  std::cout << "has_foo = " << has_foo << std::endl;
  std::cout << "has_bar = " << has_bar << std::endl;
  return 0;
}
""",
        "expected": [
            "count = 2",
            "has_foo = 1",
            "has_bar = 1",
        ],
    },
    {
        "name": "print_expr_with_replacements_original",
        "input": "tests/test_input_print_expr_with_replacements.cc",
        "preamble": """
#include <cstdio>

namespace std {

class string {
  static constexpr int Max = 1024;
  char Data[Max];
  int Len;
public:
  string() { Data[0] = '\\0'; Len = 0; }
  string(const char *S) {
    Len = 0;
    while (S[Len] && Len < Max - 1) { Data[Len] = S[Len]; ++Len; }
    Data[Len] = '\\0';
  }
  string(const string &O) {
    Len = O.Len;
    for (int i = 0; i <= Len; ++i) Data[i] = O.Data[i];
  }
  string &operator=(const string &O) {
    Len = O.Len;
    for (int i = 0; i <= Len; ++i) Data[i] = O.Data[i];
    return *this;
  }
  string &operator+=(const string &O) {
    for (int i = 0; i < O.Len && Len < Max - 1; ++i)
      Data[Len++] = O.Data[i];
    Data[Len] = '\\0';
    return *this;
  }
  string &operator+=(const char *S) {
    int i = 0;
    while (S[i] && Len < Max - 1)
      Data[Len++] = S[i++];
    Data[Len] = '\\0';
    return *this;
  }
  friend string operator+(const string &A, const string &B) {
    string R = A; R += B; return R;
  }
  friend string operator+(const string &A, const char *B) {
    string R = A; R += B; return R;
  }
  friend string operator+(const char *A, const string &B) {
    string R = A; R += B; return R;
  }
  bool operator==(const string &O) const {
    if (Len != O.Len) return false;
    for (int i = 0; i < Len; ++i)
      if (Data[i] != O.Data[i]) return false;
    return true;
  }
  bool operator!=(const string &O) const { return !(*this == O); }
  int size() const { return Len; }
  const char *c_str() const { return Data; }
};

template <typename A, typename B>
struct pair {
  A first;
  B second;
  pair() = default;
  pair(const A &a, const B &b) : first(a), second(b) {}
};

template <typename K, typename V>
class unordered_map {
public:
  static constexpr int Max = 16;
  pair<K, V> Items[Max];
  int Size;
  unordered_map() : Size(0) {}

  struct iterator {
    pair<K, V> *Items;
    int Idx;
    iterator(pair<K, V> *items, int idx) : Items(items), Idx(idx) {}
    pair<K, V> &operator*() const { return Items[Idx]; }
    pair<K, V> *operator->() const { return &Items[Idx]; }
    iterator &operator++() { ++Idx; return *this; }
    bool operator!=(const iterator &O) const { return Idx != O.Idx; }
  };

  iterator find(const K &Key) const {
    for (int i = 0; i < Size; ++i) {
      if (Items[i].first == Key)
        return iterator(const_cast<pair<K, V>*>(&Items[i]), i);
    }
    return end();
  }

  iterator end() const { return iterator(nullptr, Size); }

  V &operator[](const K &Key) {
    for (int i = 0; i < Size; ++i) {
      if (Items[i].first == Key)
        return Items[i].second;
    }
    Items[Size].first = Key;
    Items[Size].second = V();
    return Items[Size++].second;
  }
};

template <typename T>
class vector {
public:
  static constexpr int Max = 256;
  T Items[Max];
  int Size;
  vector() : Size(0) {}
  explicit vector(unsigned n) : Size(static_cast<int>(n)) {
    for (int i = 0; i < Size; ++i) Items[i] = T();
  }
  void push_back(const T &V) { Items[Size++] = V; }
  void pop_back() { --Size; }
  T &back() { return Items[Size - 1]; }
  const T &back() const { return Items[Size - 1]; }
  T &operator[](int i) { return Items[i]; }
  const T &operator[](int i) const { return Items[i]; }
  bool empty() const { return Size == 0; }
};

} // namespace std

struct ASTContext {};

struct Stmt {
  virtual ~Stmt() = default;
};

struct Expr : Stmt {};

struct StringRefLike {
  std::string S;
  StringRefLike(const std::string &s = "") : S(s) {}
  std::string str() const { return S; }
};

struct TypeInfo {
  std::string Name;
  TypeInfo(const std::string &n = "") : Name(n) {}
  std::string getAsString() const { return Name; }
};

struct MemberNameInfo {
  std::string Name;
  MemberNameInfo(const std::string &n = "") : Name(n) {}
  std::string getAsString() const { return Name; }
};

enum Opcode { BO_Add };
enum UOpcode { UO_PreInc, UO_PostInc };

struct BinaryOperator : Expr {
  Opcode Op;
  Expr *LHS;
  Expr *RHS;
  BinaryOperator(Expr *L, Expr *R) : Op(BO_Add), LHS(L), RHS(R) {}
  const Expr *getLHS() const { return LHS; }
  const Expr *getRHS() const { return RHS; }
  StringRefLike getOpcodeStr() const { return StringRefLike("+"); }
};

struct UnaryOperator : Expr {
  UOpcode Op;
  Expr *Sub;
  UnaryOperator(UOpcode o, Expr *s) : Op(o), Sub(s) {}
  UOpcode getOpcode() const { return Op; }
  const Expr *getSubExpr() const { return Sub; }
  bool isPostfix() const { return Op == UO_PostInc; }
  StringRefLike getOpcodeStr(UOpcode) const {
    return StringRefLike("++");
  }
};

struct CallExpr : Expr {
  Expr *CalleeExpr;
  Expr *Args[8];
  unsigned NumArgs;
  CallExpr(Expr *callee, unsigned n = 0) : CalleeExpr(callee), NumArgs(n) {
    for (unsigned i = 0; i < 8; ++i) Args[i] = nullptr;
  }
  void setArg(unsigned i, Expr *a) { Args[i] = a; }
  const Expr *getCallee() const { return CalleeExpr; }
  unsigned getNumArgs() const { return NumArgs; }
  const Expr *getArg(unsigned i) const { return Args[i]; }
};

struct ConditionalOperator : Expr {
  Expr *Cond;
  Expr *True;
  Expr *False;
  ConditionalOperator(Expr *c, Expr *t, Expr *f)
      : Cond(c), True(t), False(f) {}
  const Expr *getCond() const { return Cond; }
  const Expr *getTrueExpr() const { return True; }
  const Expr *getFalseExpr() const { return False; }
};

struct ArraySubscriptExpr : Expr {
  Expr *Base_;
  Expr *Idx_;
  ArraySubscriptExpr(Expr *b, Expr *i) : Base_(b), Idx_(i) {}
  const Expr *getBase() const { return Base_; }
  const Expr *getIdx() const { return Idx_; }
};

struct MemberExpr : Expr {
  Expr *Base_;
  bool Arrow;
  MemberNameInfo Info;
  MemberExpr(Expr *b, bool a, const std::string &n)
      : Base_(b), Arrow(a), Info(n) {}
  const Expr *getBase() const { return Base_; }
  bool isArrow() const { return Arrow; }
  MemberNameInfo getMemberNameInfo() const { return Info; }
};

struct ParenExpr : Expr {
  Expr *Sub;
  ParenExpr(Expr *s) : Sub(s) {}
  const Expr *getSubExpr() const { return Sub; }
};

struct ImplicitCastExpr : Expr {
  Expr *Sub;
  ImplicitCastExpr(Expr *s) : Sub(s) {}
  const Expr *getSubExpr() const { return Sub; }
};

struct CStyleCastExpr : Expr {
  TypeInfo Type;
  Expr *Sub;
  CStyleCastExpr(const std::string &t, Expr *s) : Type(t), Sub(s) {}
  TypeInfo getTypeAsWritten() const { return Type; }
  const Expr *getSubExpr() const { return Sub; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) {
  return dynamic_cast<const T *>(P);
}

std::string PrintExpr(const Expr *E, const ASTContext *Ctx) {
  return "<expr>";
}
""",
        "main": """
int main() {
  Expr a, b;
  BinaryOperator add(&a, &b);
  std::unordered_map<const Expr *, std::string> Repls;
  Repls[&add] = "REPLACED";
  std::printf("leaf = %s\\n", PrintExprWithReplacements(&a, Repls, nullptr).c_str());
  std::printf("replaced = %s\\n", PrintExprWithReplacements(&add, Repls, nullptr).c_str());
  Expr foo;
  CallExpr call(&foo, 2);
  call.setArg(0, &a);
  call.setArg(1, &b);
  std::printf("call = %s\\n", PrintExprWithReplacements(&call, Repls, nullptr).c_str());
  return 0;
}
""",
        "expected": [
            "leaf = <expr>",
            "replaced = REPLACED",
            "call = <expr>(<expr>, <expr>)",
        ],
    },
    {
        "name": "collect_group_holes_original",
        "input": "tests/test_input_collect_group_holes.cc",
        "preamble": """
#include <string>
#include <vector>

namespace std {

template <typename T>
class unordered_set {
public:
  static constexpr int Max = 16;
  std::string Items[Max];
  int Size;
  unordered_set() : Size(0) {}
  void insert(const std::string &V) {
    for (int i = 0; i < Size; ++i)
      if (Items[i] == V)
        return;
    Items[Size++] = V;
  }
  bool count(const std::string &V) const {
    for (int i = 0; i < Size; ++i)
      if (Items[i] == V)
        return true;
    return false;
  }
};

} // namespace std

struct StmtRange;

struct Stmt {
  virtual ~Stmt() = default;
  virtual StmtRange children() const;
};

struct StmtRange {
  static constexpr int Max = 16;
  const Stmt *Items[Max];
  int Size;
  StmtRange() : Size(0) {}
  void push_back(const Stmt *S) { Items[Size++] = S; }
  const Stmt *const *begin() const { return Items; }
  const Stmt *const *end() const { return Items + Size; }

  struct RevIt {
    const Stmt *const *P;
    const Stmt *operator*() { return *(P - 1); }
    RevIt &operator++() { --P; return *this; }
    bool operator!=(const RevIt &O) const { return P != O.P; }
  };
  RevIt rbegin() { return RevIt{Items + Size}; }
  RevIt rend() { return RevIt{Items}; }
};

StmtRange Stmt::children() const { return StmtRange(); }

struct Expr : Stmt {
  StmtRange children() const override { return StmtRange(); }
};

struct FunctionDecl {
  std::string Name;
  FunctionDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
};

struct CallExpr : Expr {
  FunctionDecl *Callee;
  StmtRange Args;
  CallExpr(FunctionDecl *C) : Callee(C) {}
  void addArg(Expr *A) { Args.push_back(A); }
  const FunctionDecl *getDirectCallee() const { return Callee; }
  unsigned getNumArgs() const { return static_cast<unsigned>(Args.Size); }
  const Expr *getArg(unsigned i) const { return static_cast<const Expr *>(Args.Items[i]); }
  StmtRange children() const override {
    StmtRange R;
    for (const Stmt *S : Args)
      R.push_back(S);
    return R;
  }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

template <typename T, typename U>
const T *dyn_cast_or_null(const U *P) {
  return P ? dynamic_cast<const T *>(P) : nullptr;
}
""",
        "main": """
#include <iostream>
int main() {
  FunctionDecl f("foo"), g("bar"), h("baz");
  std::unordered_set<std::string> group;
  group.insert("foo");
  group.insert("bar");
  Expr leaf;
  CallExpr call_f(&f);
  call_f.addArg(&leaf);
  CallExpr call_g(&g);
  call_g.addArg(&call_f);
  CallExpr call_h(&h);
  call_h.addArg(&call_g);
  std::vector<CallExpr *> holes;
  CollectGroupHoles(&call_h, group, holes);
  std::cout << "count = " << holes.size() << std::endl;
  bool has_f = false, has_g = false;
  for (int i = 0; i < holes.size(); ++i) {
    if (holes[i]->getDirectCallee()->getNameAsString() == "foo") has_f = true;
    if (holes[i]->getDirectCallee()->getNameAsString() == "bar") has_g = true;
  }
  std::cout << "has_foo = " << has_f << std::endl;
  std::cout << "has_bar = " << has_g << std::endl;
  return 0;
}
""",
        "expected": [
            "count = 1",
            "has_foo = 0",
            "has_bar = 1",
        ],
    },
]


def run_test(test):
    print(f"--- Testing {test['name']} ---")

    # 1. Run transpiler
    result = subprocess.run(
        [TRANSPILER, test["input"], "--"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"FAIL: transpiler exited with code {result.returncode}")
        print(result.stderr)
        return False

    # 2. Strip diagnostic lines
    lines = []
    for line in result.stdout.splitlines():
        stripped = line.strip()
        if stripped.startswith("[Detected"):
            continue
        if stripped.startswith("// =="):
            continue
        if stripped.startswith("// Generated"):
            continue
        lines.append(line)
    generated = "\n".join(lines)

    # 3. Write combined source
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".cc", delete=False
    ) as f:
        if "preamble" in test:
            f.write(test["preamble"])
            f.write("\n")
        f.write(generated)
        f.write("\n")
        f.write(test["main"])
        src_path = f.name

    # 4. Compile
    exe_path = src_path.replace(".cc", "")
    compile_result = subprocess.run(
        ["clang++", "-std=c++17", src_path, "-o", exe_path],
        capture_output=True,
        text=True,
    )
    if compile_result.returncode != 0:
        print(f"FAIL: compilation failed")
        print(compile_result.stderr)
        os.unlink(src_path)
        return False

    # 5. Run
    run_result = subprocess.run([exe_path], capture_output=True, text=True)
    if run_result.returncode != 0:
        print(f"FAIL: runtime error")
        print(run_result.stderr)
        os.unlink(src_path)
        os.unlink(exe_path)
        return False

    # 6. Verify output
    actual_lines = [line.rstrip() for line in run_result.stdout.splitlines()]
    expected_lines = test["expected"]
    if actual_lines != expected_lines:
        print(f"FAIL: output mismatch")
        print("Expected:")
        for line in expected_lines:
            print(f"  {line}")
        print("Actual:")
        for line in actual_lines:
            print(f"  {line}")
        os.unlink(src_path)
        os.unlink(exe_path)
        return False

    print(f"PASS")
    os.unlink(src_path)
    os.unlink(exe_path)
    return True


def main():
    if not os.path.exists(TRANSPILER):
        print(f"Transpiler not found: {TRANSPILER}")
        print("Please build first: mkdir build && cd build && cmake .. && make")
        sys.exit(1)

    passed = 0
    failed = 0
    for test in TESTS:
        if run_test(test):
            passed += 1
        else:
            failed += 1
        print()

    print(f"Results: {passed} passed, {failed} failed")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
