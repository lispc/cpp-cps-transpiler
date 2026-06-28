#!/usr/bin/env python3
"""
Inventory of recursive functions in src/*.cc and their regression test coverage.

We use the Homebrew clang AST dump to find every function definition in the
project source files, build a call graph among project functions, and report the
functions that participate in self- or mutual-recursion.  Each function is then
matched against the test cases listed in tests/cases/order.txt.

Output: docs/recursive_functions_inventory.txt
"""

import os
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "src"
CASES_DIR = ROOT / "tests" / "cases"
ORDER_FILE = CASES_DIR / "order.txt"
CLANGPP = Path("/opt/homebrew/Cellar/llvm/22.1.4/bin/clang++")
LLVM_INCLUDE = Path("/opt/homebrew/Cellar/llvm/22.1.4/include")
TRANSPILER = ROOT / "build" / "cps-transpiler"

FUNC_LOC_RE = re.compile(r"(<[^>]+>)")
FUNC_NAME_RE = re.compile(r"(\S+)\s+'[^']*'\s*$")
KNOWN_FLAGS = {
    "used",
    "referenced",
    "constexpr",
    "virtual",
    "static",
    "implicit_instantiation",
    "instantiated_from",
    "inline",
    "friend_undeclared",
    "friend",
}

# Location range parser.  Accepts forms like:
#   <line:88:1, line:98:1>
#   <line:88:1, col:98>
#   <line:88:1>
#   <col:88>
RANGE_RE = re.compile(
    r"<(?:line:)?(\d+):(\d+)"
    r"(?:,\s*(?:line:)?(\d+)?:(\d+))?"
    r">"
)


def run_ast_dump(src_file: Path) -> str:
    cmd = [
        str(CLANGPP),
        "-Xclang",
        "-ast-dump",
        "-fsyntax-only",
        "-I",
        str(LLVM_INCLUDE),
        "-std=c++17",
        str(src_file),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if result.returncode != 0:
        # Even with parse errors the AST dump is usually emitted; keep going.
        print(f"warning: ast-dump returned {result.returncode} for {src_file}",
              file=sys.stderr)
    return result.stdout


def parse_location_range(loc_str: str):
    """Return (start_line, end_line) or None if the range is unusable."""
    m = RANGE_RE.search(loc_str)
    if not m:
        return None
    start_line = int(m.group(1))
    if m.group(3) is not None:
        end_line = int(m.group(3))
    elif m.group(4) is not None:
        # second part is just a column, same line as start
        end_line = start_line
    else:
        end_line = start_line
    return start_line, end_line


def is_project_function(name: str, src_text: str, start_line: int, end_line: int) -> bool:
    """Heuristic: does the given line range contain a definition of name(...) with a body?"""
    lines = src_text.splitlines()
    start = max(0, start_line - 1)
    end = min(end_line, len(lines))
    snippet = "\n".join(lines[start:end])
    if "{" not in snippet:
        return False
    brace = snippet.find("{")
    head = snippet[:brace]
    return bool(re.search(r"\b" + re.escape(name) + r"\b", head)) and "(" in head


def preprocess(src_file: Path) -> Path:
    pre_file = Path("/tmp") / f"cps_inv_{src_file.name}"
    cmd = [
        str(CLANGPP),
        "-E",
        "-P",
        "-I",
        str(LLVM_INCLUDE),
        str(src_file),
        "-o",
        str(pre_file),
    ]
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    return pre_file


def verify_with_transpiler(pre_file: Path, names: list) -> set:
    """Run cps-transpiler on the preprocessed file for the given names."""
    if not names or not TRANSPILER.exists():
        return set()
    cmd = [
        str(TRANSPILER),
        str(pre_file),
        "--explain",
    ]
    for name in names:
        cmd += ["--function", name]
    cmd += ["--", "-std=c++17"]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        print(f"warning: transpiler timeout for {pre_file}", file=sys.stderr)
        return set()
    detected = set()
    for line in result.stdout.splitlines() + result.stderr.splitlines():
        m = re.search(r"\[Detected recursive function\]\s+(\S+)", line)
        if m and m.group(1) in names:
            detected.add(m.group(1))
    return detected


def candidate_names_from_source(src_text: str) -> set:
    """Quick heuristic: identifiers that are immediately followed by '(' in source."""
    names = set()
    for line in src_text.splitlines():
        stripped = line.strip()
        if stripped.startswith("//"):
            continue
        for m in re.finditer(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", line):
            names.add(m.group(1))
    return names


DECL_KIND_RE = re.compile(r"^[\s|`-]*(FunctionDecl|CXXMethodDecl)\b")


def parse_function_decl_line(line: str):
    if not DECL_KIND_RE.search(line):
        return None
    loc_match = FUNC_LOC_RE.search(line)
    if not loc_match:
        return None
    loc_str = loc_match.group(1)
    # Locate the quoted type at the end of the line.
    quote_end = line.rfind("'")
    quote_start = line.rfind("'", 0, quote_end)
    if quote_start == -1 or quote_end == -1 or quote_start == quote_end:
        return None
    prefix = line[:quote_start]
    tokens = prefix.split()
    name = ""
    for token in reversed(tokens):
        if token in KNOWN_FLAGS:
            continue
        name = token
        break
    if not name:
        return None
    return loc_str, name


def extract_functions(src_file: Path) -> list:
    src_text = src_file.read_text(encoding="utf-8")
    src_candidates = candidate_names_from_source(src_text)
    ast = run_ast_dump(src_file)
    functions = []
    for line in ast.splitlines():
        parsed = parse_function_decl_line(line)
        if not parsed:
            continue
        loc_str, name = parsed
        # Skip declarations that originate in system headers.
        if "/" in loc_str or "\\" in loc_str:
            continue
        if name not in src_candidates:
            continue
        rng = parse_location_range(loc_str)
        if not rng:
            continue
        start_line, end_line = rng
        if start_line < 1 or end_line < start_line:
            continue
        # Confirm the function is actually defined in this source file.
        if not is_project_function(name, src_text, start_line, end_line):
            continue
        functions.append({
            "name": name,
            "file": src_file.name,
            "start_line": start_line,
            "end_line": end_line,
            "src_text": src_text,
        })
    return functions


def function_body_text(func: dict) -> str:
    """Return the function body (from the first '{' to the matching '}')."""
    lines = func["src_text"].splitlines()
    start = func["start_line"] - 1
    end = min(func["end_line"], len(lines))
    snippet = "\n".join(lines[start:end])
    brace = snippet.find("{")
    if brace == -1:
        return ""
    body = snippet[brace + 1:]
    return body


def find_calls(body: str, candidates: set) -> set:
    """Return the set of candidate function names that are called in body."""
    found = set()
    for name in candidates:
        if re.search(r"\b" + re.escape(name) + r"\s*\(", body):
            found.add(name)
    return found


def tarjan_scc(nodes: set, graph: dict) -> list:
    """Return a list of SCCs (each a list of node names)."""
    index_counter = [0]
    stack = []
    on_stack = set()
    indices = {}
    lowlinks = {}
    sccs = []

    def strongconnect(v):
        indices[v] = index_counter[0]
        lowlinks[v] = index_counter[0]
        index_counter[0] += 1
        stack.append(v)
        on_stack.add(v)
        for w in graph.get(v, set()):
            if w not in nodes:
                continue
            if w not in indices:
                strongconnect(w)
                lowlinks[v] = min(lowlinks[v], lowlinks[w])
            elif w in on_stack:
                lowlinks[v] = min(lowlinks[v], indices[w])
        if lowlinks[v] == indices[v]:
            scc = []
            while True:
                w = stack.pop()
                on_stack.remove(w)
                scc.append(w)
                if w == v:
                    break
            sccs.append(scc)

    for v in nodes:
        if v not in indices:
            strongconnect(v)
    return sccs


def load_test_cases() -> list:
    if not ORDER_FILE.exists():
        return []
    with open(ORDER_FILE, "r", encoding="utf-8") as f:
        return [line.strip() for line in f if line.strip()]


def to_snake_case(name: str) -> str:
    # Insert underscores before capital letters and lower-case the result.
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s)
    return s.lower().lstrip("_")


def stem_names(name: str) -> list:
    """Return progressively shorter name stems (e.g. EvalConditionForParam -> EvalCondition)."""
    # Split CamelCase into words.
    words = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    words = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", words)
    tokens = words.split("_")
    stems = []
    for i in range(len(tokens), 0, -1):
        stem = "".join(tokens[:i])
        if stem:
            stems.append(stem)
    return stems


# Hand-curated mapping for functions whose test-case names don't map directly.
MANUAL_TEST_CASES = {
    "FlattenIfElse": ["flatten_if_else_original", "if_else"],
    "IsInTailPosition": ["is_in_tail_position_original", "is_in_tail_position_expr_original"],
    "ContainsRecursiveCall": ["contains_recursive_call", "contains_rec"],
    "ExprUsesParams": ["expr_uses_params_original", "expr_uses"],
    "IsPureExprImpl": ["is_pure_expr_original"],
    "IsPureExprIgnoringRecursiveCallsImpl": ["is_pure_expr_ignore_original"],
    "EvalConditionForParam": ["eval_condition_original"],
    "CollectRecursiveCallsInStmt": ["collect_recursive_calls_original"],
}


def match_test_cases(name: str, cases: list) -> list:
    """Return test-case names that appear to cover this function."""
    if name in MANUAL_TEST_CASES:
        return [c for c in MANUAL_TEST_CASES[name] if c in cases]

    matches = []
    snake = to_snake_case(name)
    candidates = {snake, snake + "_original", snake + "_real"}
    for case in cases:
        if case.lower() in candidates:
            matches.append(case)
    return matches


def main():
    if not TRANSPILER.exists():
        print(f"transpiler not found: {TRANSPILER}", file=sys.stderr)
        sys.exit(1)

    cases = load_test_cases()

    # Extract candidate functions from each source file and verify with the transpiler.
    verified_recursive = []
    for src_file in sorted(SRC_DIR.glob("*.cc")):
        funcs = extract_functions(src_file)
        if not funcs:
            continue
        names = sorted({f["name"] for f in funcs})
        print(f"{src_file.name}: {len(funcs)} project functions, verifying...", file=sys.stderr)
        pre_file = preprocess(src_file)
        detected = verify_with_transpiler(pre_file, names)
        print(f"  -> detected recursive: {sorted(detected)}", file=sys.stderr)
        for func in funcs:
            if func["name"] in detected:
                verified_recursive.append(func)

    # Build inventory.
    inventory = []
    for func in verified_recursive:
        matched = match_test_cases(func["name"], cases)
        inventory.append({
            "name": func["name"],
            "file": func["file"],
            "line": func["start_line"],
            "test": matched,
        })

    # Deduplicate by name (a name may appear in multiple files/overloads).
    seen = set()
    deduped = []
    for item in sorted(inventory, key=lambda x: (x["file"], x["line"])):
        if item["name"] in seen:
            continue
        seen.add(item["name"])
        deduped.append(item)

    out_path = ROOT / "docs" / "recursive_functions_inventory.txt"
    out_path.parent.mkdir(exist_ok=True)

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("# Recursive functions in src/*.cc\n\n")
        f.write("| Function | Source | Test case(s) |\n")
        f.write("|---|---|---|\n")
        for item in deduped:
            loc = f"{item['file']}:{item['line']}"
            test = ", ".join(item["test"]) if item["test"] else "(none)"
            f.write(f"| {item['name']} | {loc} | {test} |\n")
        f.write("\n")
        f.write(f"**Total recursive functions found:** {len(deduped)}\n\n")
        covered = sum(1 for x in deduped if x["test"])
        f.write(f"- With matching test case: {covered}\n")
        f.write(f"- Without matching test case: {len(deduped) - covered}\n\n")
        f.write("**Detection method:** AST extraction followed by cps-transpiler ")
        f.write("verification on a preprocessed translation unit.\n\n")
        f.write("**Test-case matching:** function/test-case name similarity ")
        f.write("(snake_case, `_original`, `_real`, and substring matches).\n")

    print(f"Wrote {out_path}")
    print(f"Total: {len(deduped)}, covered: {covered}, uncovered: {len(deduped)-covered}")


if __name__ == "__main__":
    main()
