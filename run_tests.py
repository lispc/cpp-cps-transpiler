#!/usr/bin/env python3
"""
Automated regression tests for cps-transpiler.

Test cases live under tests/cases/<name>/:
  - main.cc       driver program
  - preamble.cc   optional extra code injected before generated CPS code
  - expected.txt  expected stdout, one line per line

For each case the runner:
  1. Runs the transpiler on tests/test_input_<name>.cc
  2. Strips diagnostic comments
  3. Compiles generated code with preamble.cc + main.cc
  4. Runs and verifies expected output
"""

import sys
import os

import cps_testlib as cps

TRANSPILER = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "build", "cps-transpiler"
)

CASES_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "tests", "cases"
)


def load_case(name):
    """Load a single test case from its directory."""
    case_dir = os.path.join(CASES_DIR, name)

    with open(os.path.join(case_dir, "input.txt"), "r", encoding="utf-8") as f:
        input_path = f.read().strip()

    with open(os.path.join(case_dir, "main.cc"), "r", encoding="utf-8") as f:
        main = f.read()

    preamble = None
    preamble_path = os.path.join(case_dir, "preamble.cc")
    if os.path.exists(preamble_path):
        with open(preamble_path, "r", encoding="utf-8") as f:
            preamble = f.read()

    expected = []
    expected_path = os.path.join(case_dir, "expected.txt")
    if os.path.exists(expected_path):
        with open(expected_path, "r", encoding="utf-8") as f:
            expected = [line.rstrip("\n") for line in f]

    return {
        "name": name,
        "input": input_path,
        "main": main,
        "preamble": preamble,
        "expected": expected,
    }


def load_tests():
    """Load all test cases in tests/cases/order.txt order."""
    order_path = os.path.join(CASES_DIR, "order.txt")
    if not os.path.exists(order_path):
        raise RuntimeError(f"Case order file not found: {order_path}")

    with open(order_path, "r", encoding="utf-8") as f:
        names = [line.strip() for line in f if line.strip()]

    return [load_case(name) for name in names]


def run_test(test):
    print(f"--- Testing {test['name']} ---")

    try:
        raw = cps.run_transpiler(TRANSPILER, test["input"])
        generated = cps.strip_diagnostic_lines(raw)
        out = cps.compile_and_run(
            generated, test["main"], test.get("preamble")
        )
        cps.check_output(out, test["expected"])
        print("PASS")
        return True
    except cps.CpsTestError as e:
        print(f"FAIL: {e.stage}")
        if e.message:
            print(e.message)
        if e.stderr:
            print(e.stderr)
        if e.stage == "verify" and e.stdout:
            print("Expected:")
            for line in test["expected"]:
                print(f"  {line}")
            print("Actual:")
            for line in e.stdout.splitlines():
                print(f"  {line}")
        return False


def main():
    if not os.path.exists(TRANSPILER):
        print(f"Transpiler not found: {TRANSPILER}")
        print("Please build first: mkdir build && cd build && cmake .. && make")
        sys.exit(1)

    tests = load_tests()

    passed = 0
    failed = 0
    for test in tests:
        if run_test(test):
            passed += 1
        else:
            failed += 1
        print()

    print(f"Results: {passed} passed, {failed} failed")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
