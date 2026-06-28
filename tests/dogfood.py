#!/usr/bin/env python3
"""
Dogfood runner for cps-transpiler.

The project already contains test inputs that are direct copies of recursive
helpers used inside the transpiler itself (e.g. ContainsRecursiveCall,
IsInTailPosition, ParseLinearTerms).  This script runs the transpiler on all of
those inputs, reports which transformation rule was selected for each helper,
and summarises how much of the transpiler's own recursive code it can currently
eliminate.

Usage:
    python3 tests/dogfood.py
"""

import os
import re
import subprocess
import sys

TRANSPILER = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "build",
    "cps-transpiler",
)

TESTS_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "tests",
)

# Match comments like "Direct copy of Foo from src/..."
DIRECT_COPY_RE = re.compile(
    r"Direct copy of\s+(\w+)\s+from\s+src/", re.IGNORECASE
)

RULE_RE = re.compile(r"Generated\s+([-\w]+)\s+code for function")


def run_transpiler(path):
    try:
        result = subprocess.run(
            [TRANSPILER, path],
            capture_output=True,
            text=True,
            timeout=30,
        )
        return result.returncode, result.stdout, result.stderr
    except Exception as e:
        return -1, "", str(e)


def main():
    if not os.path.exists(TRANSPILER):
        print(f"Transpiler not found: {TRANSPILER}")
        sys.exit(1)

    candidates = []
    for name in sorted(os.listdir(TESTS_DIR)):
        if not name.startswith("test_input_") or not name.endswith(".cc"):
            continue
        path = os.path.join(TESTS_DIR, name)
        with open(path, "r", encoding="utf-8") as f:
            head = f.read(2048)
        m = DIRECT_COPY_RE.search(head)
        if not m:
            continue
        candidates.append((m.group(1), path))

    if not candidates:
        print("No dogfood candidates found.")
        sys.exit(0)

    transformed = 0
    unsupported = 0
    errored = 0

    print(f"Running dogfood on {len(candidates)} transpiler helpers...\n")
    for helper, path in candidates:
        rc, stdout, stderr = run_transpiler(path)
        if rc != 0:
            print(f"[ERROR] {helper:40s} transpiler crashed")
            print(f"        {stderr.strip()[:200]}")
            errored += 1
            continue

        m = RULE_RE.search(stdout)
        if m:
            rule = m.group(1)
            print(f"[OK]    {helper:40s} -> {rule}")
            transformed += 1
        else:
            print(f"[NONE]  {helper:40s} -> no applicable rule")
            unsupported += 1

    print("\nSummary:")
    print(f"  Transformed: {transformed}/{len(candidates)}")
    print(f"  Unsupported: {unsupported}/{len(candidates)}")
    print(f"  Errors:      {errored}/{len(candidates)}")
    sys.exit(0 if errored == 0 else 1)


if __name__ == "__main__":
    main()
