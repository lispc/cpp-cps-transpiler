"""Shared helpers for running and verifying cps-transpiler test cases.

This module is meant to be reused by run_tests.py, fuzz harnesses, and
benchmarks so that the "transpile -> compile -> run -> compare" pipeline is
defined in one place.
"""

import os
import subprocess
import tempfile


class CpsTestError(Exception):
    """Raised when any stage of a CPS test case fails."""

    def __init__(self, stage, message, stdout="", stderr=""):
        super().__init__(message)
        self.stage = stage
        self.message = message
        self.stdout = stdout
        self.stderr = stderr

    def __str__(self):
        parts = [f"[{self.stage}] {self.message}"]
        if self.stdout:
            parts.append("stdout:\n" + self.stdout)
        if self.stderr:
            parts.append("stderr:\n" + self.stderr)
        return "\n".join(parts)


DIAGNOSTIC_PREFIXES = ("[Detected", "// ==", "// Generated")


def run_transpiler(transpiler, input_path, extra_args=None):
    """Run the transpiler on input_path and return its raw stdout."""
    cmd = [transpiler, input_path, "--"]
    if extra_args:
        cmd.extend(extra_args)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise CpsTestError(
            "transpile",
            f"transpiler exited with code {result.returncode}",
            result.stdout,
            result.stderr,
        )
    return result.stdout


def strip_diagnostic_lines(stdout):
    """Remove diagnostic banner lines emitted by the transpiler."""
    lines = []
    for line in stdout.splitlines():
        stripped = line.strip()
        if any(stripped.startswith(p) for p in DIAGNOSTIC_PREFIXES):
            continue
        lines.append(line)
    return "\n".join(lines)


def compile_and_run(
    generated,
    main,
    preamble=None,
    compiler="clang++",
    std="c++17",
    extra_compile_args=None,
):
    """Compile generated code together with an optional preamble and main().

    Returns the stdout of the resulting executable.
    """
    src_path = None
    exe_path = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", suffix=".cc", delete=False) as f:
            if preamble:
                f.write(preamble)
                f.write("\n")
            f.write(generated)
            f.write("\n")
            f.write(main)
            src_path = f.name

        exe_path = src_path.replace(".cc", "")
        cmd = [compiler, f"-std={std}"]
        if extra_compile_args:
            cmd.extend(extra_compile_args)
        cmd.extend([src_path, "-o", exe_path])

        comp = subprocess.run(cmd, capture_output=True, text=True)
        if comp.returncode != 0:
            raise CpsTestError("compile", "compilation failed", "", comp.stderr)

        run = subprocess.run([exe_path], capture_output=True, text=True)
        if run.returncode != 0:
            raise CpsTestError("run", "runtime error", run.stdout, run.stderr)

        return run.stdout
    finally:
        for p in (src_path, exe_path):
            if p:
                try:
                    os.unlink(p)
                except FileNotFoundError:
                    pass


def check_output(stdout, expected):
    """Compare program output line-by-line with expected lines."""
    actual_lines = [line.rstrip() for line in stdout.splitlines()]
    if actual_lines != expected:
        raise CpsTestError("verify", "output mismatch", stdout, "")
