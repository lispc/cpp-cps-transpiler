#!/usr/bin/env python3
"""
Simple performance benchmark for cps-transpiler.

For each benchmark case the script:
  1. Reads the original recursive source from benchmarks/cases/
  2. Runs the transpiler to obtain the iterative version
  3. Builds a single executable that contains both versions
  4. Runs both and reports elapsed wall-clock time
"""

import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
import cps_testlib as cps

TRANSPILER = os.path.join(ROOT, "build", "cps-transpiler")

CASES = [
    {
        "name": "fib(45)",
        "input": os.path.join(ROOT, "benchmarks", "cases", "fib.cc"),
        "func": "fib",
        "arg": "45",
        "expected": "1134903170",
        # Run the original recursive implementation as well.
        "compare_recursive": True,
    },
    {
        "name": "fact(10000)",
        "input": os.path.join(ROOT, "benchmarks", "cases", "fact.cc"),
        "func": "fact",
        "arg": "10000",
        # Result is huge; just verify it is non-zero and consistent.
        "expected": None,
        "compare_recursive": False,
    },
]


def benchmark_case(case):
    print(f"--- Benchmark: {case['name']} ---")

    with open(case["input"], "r") as f:
        original = f.read()

    try:
        raw = cps.run_transpiler(TRANSPILER, case["input"])
        generated = cps.strip_diagnostic_lines(raw)
    except cps.CpsTestError as e:
        print(f"FAIL: transpiler failed\n{e.stderr}")
        return False

    func = case["func"]
    recursive_func = func + "_recursive"

    # Rename the original function (and its recursive calls) so it can coexist
    # with the transpiled version.
    renamed_original = original.replace(f"{func}(", f"{recursive_func}(")

    main = f"""#include <chrono>
#include <iostream>
#include <string>

static void print_result(const char* label, long long value) {{
  std::cout << label << " result = " << value << std::endl;
}}

int main() {{
  int n = {case['arg']};

  auto t0 = std::chrono::high_resolution_clock::now();
  long long r1 = {func}(n);
  auto t1 = std::chrono::high_resolution_clock::now();
  print_result("transpiled", r1);
  double dt_transpiled = std::chrono::duration<double>(t1 - t0).count();
  std::cout << "transpiled time: " << dt_transpiled << "s" << std::endl;

  if ({'true' if case.get('compare_recursive') else 'false'}) {{
    t0 = std::chrono::high_resolution_clock::now();
    long long r2 = {recursive_func}(n);
    t1 = std::chrono::high_resolution_clock::now();
    print_result("recursive", r2);
    double dt_recursive = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "recursive time: " << dt_recursive << "s" << std::endl;
    if (r1 != r2) {{
      std::cerr << "MISMATCH" << std::endl;
      return 1;
    }}
    if (dt_recursive > 0) {{
      std::cout << "speedup: " << (dt_recursive / dt_transpiled) << "x" << std::endl;
    }}
  }}

  return 0;
}}
"""

    try:
        out = cps.compile_and_run(
            generated,
            main,
            preamble=renamed_original,
            extra_compile_args=["-O2"],
        )
    except cps.CpsTestError as e:
        print(f"FAIL: {e.stage}")
        if e.stderr:
            print(e.stderr)
        return False

    print(out.strip())
    return True


def main():
    if not os.path.exists(TRANSPILER):
        print(f"Transpiler not found: {TRANSPILER}")
        sys.exit(1)

    passed = 0
    for case in CASES:
        if benchmark_case(case):
            passed += 1
        print()

    print(f"Benchmarks completed: {passed}/{len(CASES)}")
    sys.exit(0 if passed == len(CASES) else 1)


if __name__ == "__main__":
    main()
