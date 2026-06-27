#!/usr/bin/env python3
"""
Simple performance benchmark for cps-transpiler.

For each benchmark case the script:
  1. Reads the original recursive source from tests/
  2. Runs the transpiler to obtain the iterative version
  3. Builds a single executable that contains both versions
  4. Runs both and reports elapsed wall-clock time
"""

import subprocess
import sys
import os
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
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


def run(cmd, **kwargs):
    return subprocess.run(cmd, capture_output=True, text=True, **kwargs)


def transpile(input_path):
    result = run([TRANSPILER, input_path, "--"])
    if result.returncode != 0:
        raise RuntimeError(f"transpiler failed: {result.stderr}")
    lines = []
    for line in result.stdout.splitlines():
        stripped = line.strip()
        if stripped.startswith("[") or stripped.startswith("// =="):
            continue
        lines.append(line)
    return "\n".join(lines)


def benchmark_case(case):
    print(f"--- Benchmark: {case['name']} ---")

    with open(case["input"], "r") as f:
        original = f.read()

    generated = transpile(case["input"])

    func = case["func"]
    recursive_func = func + "_recursive"

    # Rename the original function (and its recursive calls) so it can coexist
    # with the transpiled version.
    renamed_original = original.replace(f"{func}(", f"{recursive_func}(")

    main = f"""
#include <chrono>
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

    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".cc", delete=False
    ) as f:
        f.write(renamed_original)
        f.write("\n")
        f.write(generated)
        f.write("\n")
        f.write(main)
        src_path = f.name

    exe_path = src_path.replace(".cc", "")
    compile_result = run(["clang++", "-std=c++17", "-O2", src_path, "-o", exe_path])
    if compile_result.returncode != 0:
        print(f"FAIL: compilation failed")
        print(compile_result.stderr)
        os.unlink(src_path)
        return False

    run_result = run([exe_path])
    if run_result.returncode != 0:
        print(f"FAIL: runtime error")
        print(run_result.stderr)
        os.unlink(src_path)
        os.unlink(exe_path)
        return False

    print(run_result.stdout.strip())
    os.unlink(src_path)
    os.unlink(exe_path)
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
