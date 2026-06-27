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
import subprocess
import sys
import tempfile

TRANSPILER = "./build/cps-transpiler"
NUM_CASES = 50
MAX_N = 20  # Keep inputs small so recursive reference does not blow the stack.


def generate_function(idx):
    """Generate a random recursive function string and its name."""
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


def run_transpiler(input_path):
    result = subprocess.run(
        [TRANSPILER, input_path, "--"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return None, result.stderr
    lines = []
    for line in result.stdout.splitlines():
        stripped = line.strip()
        if stripped.startswith("[Detected"):
            continue
        if stripped.startswith("// ="):
            continue
        if stripped.startswith("// Generated"):
            continue
        lines.append(line)
    return "\n".join(lines), None


def check_case(idx):
    name, original = generate_function(idx)

    with tempfile.TemporaryDirectory() as tmpdir:
        input_path = os.path.join(tmpdir, "input.cc")
        with open(input_path, "w") as f:
            f.write(original)

        generated, err = run_transpiler(input_path)
        if generated is None:
            print(f"Case {idx}: transpiler failed for:\n{original}\n{err}")
            return False
        if not generated.strip():
            print(f"Case {idx}: no rule applied for:\n{original}")
            return False

        # Build a check program containing the generated code and a renamed
        # reference recursive function.
        renamed = original.replace(f"int {name}(int n)", f"int ref_{name}(int n)")
        main_lines = ["#include <iostream>", "#include <cstdlib>"]
        main_lines.append(generated)
        main_lines.append(renamed)
        main_lines.append("int main() {")
        main_lines.append(f"  for (int n = 0; n <= {MAX_N}; ++n) {{")
        main_lines.append(
            f"    int a = {name}(n);"
        )
        main_lines.append(
            f"    int b = ref_{name}(n);"
        )
        main_lines.append(
            "    if (a != b) {"
        )
        main_lines.append(
            f"      std::cout << \"{name}(\" << n << \") mismatch: \""
        )
        main_lines.append(
            "                << a << \" vs \" << b << std::endl;"
        )
        main_lines.append("      return 1;")
        main_lines.append("    }")
        main_lines.append("  }")
        main_lines.append("  return 0;")
        main_lines.append("}")

        check_path = os.path.join(tmpdir, "check.cc")
        with open(check_path, "w") as f:
            f.write("\n".join(main_lines))

        exe_path = os.path.join(tmpdir, "check")
        compile_result = subprocess.run(
            ["clang++", "-std=c++17", check_path, "-o", exe_path],
            capture_output=True,
            text=True,
        )
        if compile_result.returncode != 0:
            print(f"Case {idx}: compilation failed for:\n{original}")
            print(compile_result.stderr)
            return False

        run_result = subprocess.run([exe_path], capture_output=True, text=True)
        if run_result.returncode != 0:
            print(f"Case {idx}: runtime mismatch:\n{run_result.stdout}")
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
