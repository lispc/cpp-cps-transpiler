#!/usr/bin/env python3
"""
Small fuzzer for cps-transpiler.

Generates random k-th order linear recurrences with integer coefficients,
runs the transpiler, and verifies that the generated iterative code produces
identical results to the original recursive implementation for a range of
inputs.
"""

import os
import random
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import cps_testlib as cps

TRANSPILER = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "build",
    "cps-transpiler",
)
NUM_CASES = 50
MAX_N = 20  # Keep inputs small so recursive reference does not blow the stack.


def generate_function(idx):
    """Generate a recursive function string and its name."""
    name = f"fuzz_{idx}"
    k = random.choice([2, 3])
    base_values = [random.randint(-5, 5) for _ in range(k)]
    coeffs = [random.choice([-2, -1, 1, 2]) for _ in range(k)]
    const = random.randint(-3, 3)

    lines = [f"int {name}(int n) {{"]
    for i, bv in enumerate(base_values):
        lines.append(f"  if (n == {i}) return {bv};")

    terms = []
    for order, c in zip(range(1, k + 1), coeffs):
        if c == 1:
            terms.append(f"{name}(n - {order})")
        elif c == -1:
            terms.append(f"-{name}(n - {order})")
        else:
            terms.append(f"{c} * {name}(n - {order})")
    expr = " + ".join(terms)
    if const != 0:
        expr += f" + {const}"
    lines.append(f"  return {expr};")
    lines.append("}")
    return name, "\n".join(lines)


def check_case(idx):
    name, original = generate_function(idx)

    with tempfile.TemporaryDirectory() as tmpdir:
        input_path = os.path.join(tmpdir, "input.cc")
        with open(input_path, "w") as f:
            f.write(original)

        try:
            raw = cps.run_transpiler(TRANSPILER, input_path)
            generated = cps.strip_diagnostic_lines(raw)
        except cps.CpsTestError as e:
            print(f"Case {idx}: transpiler failed for:\n{original}\n{e.stderr}")
            return False

        if not generated.strip():
            print(f"Case {idx}: no rule applied for:\n{original}")
            return False

        # Reference recursive implementation renamed so it does not clash with
        # the generated iterative version. It is appended after the generated
        # code so that the iterative definition is visible first.
        renamed = original.replace(
            f"int {name}(int n)", f"int ref_{name}(int n)"
        )

        main = renamed + f"""
#include <iostream>
#include <cstdlib>
int main() {{
  for (int n = 0; n <= {MAX_N}; ++n) {{
    int a = {name}(n);
    int b = ref_{name}(n);
    if (a != b) {{
      std::cout << "{name}(" << n << ") mismatch: " << a << " vs " << b << std::endl;
      return 1;
    }}
  }}
  return 0;
}}
"""

        try:
            out = cps.compile_and_run(generated, main)
        except cps.CpsTestError as e:
            print(f"Case {idx}: {e.stage} failed for:\n{original}")
            if e.stderr:
                print(e.stderr)
            if e.stdout:
                print(e.stdout)
            return False

        if out.strip():
            print(f"Case {idx}: runtime mismatch:\n{out}")
            return False

    return True


def main():
    if not os.path.exists(TRANSPILER):
        print(f"Transpiler not found: {TRANSPILER}")
        sys.exit(1)

    random.seed(42)
    passed = 0
    failed = 0
    for i in range(NUM_CASES):
        if check_case(i):
            passed += 1
        else:
            failed += 1

    print(f"Fuzz results: {passed} passed, {failed} failed")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
