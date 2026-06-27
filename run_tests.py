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
