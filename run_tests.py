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
