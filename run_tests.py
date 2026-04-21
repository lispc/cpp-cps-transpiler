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
