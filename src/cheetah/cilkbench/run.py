#!/usr/bin/env python3
"""Run the portable benchmark targets and keep each invocation's evidence."""
import argparse
import csv
import datetime
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time

SUITES = {
    "cilkbench": {
        "fib": (20, lambda n: [str(n)]),
        "nqueens": (8, lambda n: [str(n)]),
        "cilksort": (4096, lambda n: ["-n", str(n), "-c"]),
        "fft": (1024, lambda n: ["-n", str(n)]),
        "cholesky": (64, lambda n: ["-n", str(n), "-z", str(n * 4)]),
        "lu": (64, lambda n: ["-n", str(n), "-c"]),
        "heat": (32, lambda n: ["-nx", str(n), "-ny", str(n), "-nt", "2"]),
        "strassen": (64, lambda n: ["-n", str(n), "-c"]),
        "matmul": (32, lambda n: ["-n", str(n), "-c"]),
        "rectmul": (32, lambda n: ["-x", str(n), "-y", str(n), "-z", str(n), "-c"]),
        "cilk_for_test": (64, lambda n: [str(n), "32"]),
    },
    "compiled_test": {
        "fib": (20, lambda n: [str(n)]),
        "cilk_for_test": (64, lambda n: [str(n), "32"]),
    },
    "parlaylib": {
        "BFS": (128, lambda n: [str(n)]),
        "pagerank": (128, lambda n: [str(n)]),
    },
    "handcomp_test": {
        "fib": (20, lambda n: [str(n)]),
        "mm_dac": (32, lambda n: ["-n", str(n), "-c"]),
    },
}
SMOKE = {"cilkbench": ["fib", "cilk_for_test", "nqueens", "cilksort", "matmul"],
         "compiled_test": ["fib", "cilk_for_test"],
         "parlaylib": ["BFS", "pagerank"], "handcomp_test": ["fib", "mm_dac"]}


def positive(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return number


def correctness(benchmark, size, text):
    """Check result-bearing drivers; other runs are completion checks only."""
    if re.search(r"SORTING FAILURE|Invalid execution|Error \d+ loading spawn states|test FAILED|WRONG (?:RESULT|ANSWER)", text):
        return "failed"
    if benchmark == "fib":
        a, b = 0, 1
        for _ in range(size):
            a, b = b, a + b
        result = re.search(r"Result:\s*(\d+)", text)
        return "passed" if result and int(result[1]) == a else "failed"
    if benchmark == "cilk_for_test":
        return "passed" if "Valid execution" in text else "failed"
    if benchmark == "nqueens":
        known = {1: 1, 2: 0, 3: 0, 4: 2, 5: 10, 6: 4, 7: 40, 8: 92,
                 9: 352, 10: 724, 11: 2680, 12: 14200, 13: 73712, 14: 365596}
        result = re.search(r"Total number of solutions\s*:\s*(\d+)", text)
        count = int(result[1]) if result else (0 if "No solution found." in text else None)
        if size in known:
            return "passed" if count == known[size] else "failed"
    if benchmark == "cilksort":
        return "passed" if "Sorting successful." in text else "failed"
    if benchmark == "mm_dac":
        return "passed" if "MM_dac test passed." in text else "failed"
    if benchmark == "matmul":
        result = re.search(r"Max error\s*=\s*(\S+)", text)
        try:
            error = float(result[1]) if result else float("nan")
        except ValueError:
            error = float("nan")
        return "passed" if math.isfinite(error) and abs(error) <= 1e-6 else "failed"
    if benchmark in ("strassen", "rectmul", "lu"):
        # These -c drivers print a marker before comparing and an explicit
        # WRONG RESULT/ANSWER on mismatch, checked above.
        return "passed" if "check result" in text.lower() or "checking results" in text.lower() else "failed"
    if benchmark in ("BFS", "pagerank"):
        vertices = re.search(r"num vertices\s*=\s*(\d+)", text)
        if not vertices or int(vertices[1]) != size:
            return "failed"
        if benchmark == "BFS":
            visited = re.search(r"num vertices visited:\s*(\d+)", text)
            return "sanity-passed" if visited and 1 <= int(visited[1]) <= size else "failed"
        rank = re.search(r"maximum rank\s*=\s*(\S+)", text)
        try:
            maximum = float(rank[1]) if rank else float("nan")
        except ValueError:
            maximum = float("nan")
        return "sanity-passed" if math.isfinite(maximum) and maximum >= 0 else "failed"
    return "not-checked"


def main():
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=[*SUITES, "all"], default="cilkbench")
    parser.add_argument("--benchmarks", help="comma-separated names (default: all build targets in suite)")
    parser.add_argument("--workers", default="1,2", help="comma-separated worker counts (default: 1,2)")
    parser.add_argument("--size", type=positive, help="override input size for one selected benchmark")
    parser.add_argument("--repeats", type=positive, default=1, help="separate process runs per configuration")
    parser.add_argument("--timeout", type=positive, default=120, help="seconds per invocation (default: 120)")
    parser.add_argument("--output-dir", type=Path, help="parent for a unique result directory")
    parser.add_argument("--build-root", type=Path, default=Path(os.environ.get("BUILD_ROOT", root / "build")))
    parser.add_argument("--launcher", type=Path, default=root / "scripts" / "run-benchmark.sh")
    parser.add_argument("--smoke", action="store_true", help="small compiler/runtime checks with known-result assertions")
    parser.add_argument("--list", action="store_true", help="show build targets without running")
    parser.add_argument("--dry-run", action="store_true", help="print commands without running or creating files")
    args = parser.parse_args()
    suites = list(SUITES) if args.suite == "all" else [args.suite]
    if args.list:
        for suite in suites:
            print(f"{suite}: {', '.join(SUITES[suite])}")
        print("Preserved drivers outside these lists need further porting; see suite README.md files.")
        return 0
    try:
        workers = [positive(value) for value in args.workers.split(",")]
    except (ValueError, argparse.ArgumentTypeError):
        parser.error("--workers must contain comma-separated positive integers")
    selected = args.benchmarks.split(",") if args.benchmarks else None
    if args.size and (len(suites) != 1 or not selected or len(selected) != 1):
        parser.error("--size requires one --suite and one --benchmarks name")
    cases = []
    for suite in suites:
        names = selected or (SMOKE[suite] if args.smoke else list(SUITES[suite]))
        for name in names:
            if name not in SUITES[suite]:
                parser.error(f"{name!r} is not a build target in {suite}; use --list")
            default_size, arguments = SUITES[suite][name]
            size = args.size or default_size
            if name == "fib" and size > 46:
                parser.error("fib uses a signed 32-bit result; --size must be at most 46")
            if name in ("BFS", "pagerank") and (size < 2 or size & (size - 1)):
                parser.error("generated graph size must be a power of two and at least 2")
            executable = args.build_root.resolve() / "benchmarks" / suite / name
            for worker in workers:
                for repeat in range(1, args.repeats + 1):
                    command = [str(args.launcher.resolve()), str(executable), *arguments(size)]
                    cases.append((suite, name, size, worker, repeat, executable, command))
    if args.dry_run:
        for suite, name, size, worker, repeat, executable, command in cases:
            print(json.dumps({"suite": suite, "benchmark": name, "workers": worker,
                              "repeat": repeat, "CILK_NWORKERS": str(worker), "command": command}))
        return 0
    missing = sorted({str(case[5]) for case in cases if not case[5].is_file()})
    if missing:
        parser.error("build the benchmarks first; missing: " + ", ".join(missing))
    if not args.launcher.is_file():
        parser.error(f"benchmark launcher does not exist: {args.launcher}")
    parent = (args.output_dir or args.build_root / "results").resolve()
    run_dir = parent / (datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S") + f"-{os.getpid()}-{time.time_ns() % 1000000:06d}")
    run_dir.mkdir(parents=True, exist_ok=False)
    (run_dir / "invocation.json").write_text(json.dumps({
        "arguments": vars(args), "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "checks": "known results and driver checks where available; graph size/range checks labeled sanity-passed; see runner correctness()",
    }, default=str, indent=2) + "\n")
    failed = False
    with (run_dir / "summary.csv").open("w", newline="") as summary:
        writer = csv.DictWriter(summary, fieldnames=["suite", "benchmark", "size", "workers", "repeat", "exit_code", "wall_seconds", "correctness", "stdout", "stderr"])
        writer.writeheader()
        for suite, name, size, worker, repeat, executable, command in cases:
            stem = f"{suite}.{name}.n{size}.w{worker}.r{repeat}"
            stdout_file, stderr_file = run_dir / f"{stem}.stdout.txt", run_dir / f"{stem}.stderr.txt"
            env = dict(os.environ, CILK_NWORKERS=str(worker), BUILD_ROOT=str(args.build_root.resolve()))
            start = time.monotonic()
            with stdout_file.open("w") as stdout, stderr_file.open("w") as stderr:
                try:
                    result = subprocess.run(command, env=env, cwd=run_dir, stdout=stdout, stderr=stderr, timeout=args.timeout)
                    code = result.returncode
                except subprocess.TimeoutExpired:
                    code = 124
                    stderr.write(f"\nRunner timeout after {args.timeout} seconds\n")
                except OSError as error:
                    code = 127
                    stderr.write(str(error) + "\n")
            elapsed = time.monotonic() - start
            check = correctness(name, size, stdout_file.read_text(errors="replace") + stderr_file.read_text(errors="replace"))
            failed |= code != 0 or check == "failed"
            writer.writerow({"suite": suite, "benchmark": name, "size": size, "workers": worker,
                             "repeat": repeat, "exit_code": code, "wall_seconds": f"{elapsed:.6f}",
                             "correctness": check, "stdout": stdout_file.name, "stderr": stderr_file.name})
            summary.flush()
            print(f"{suite}/{name}: workers={worker}, exit={code}, correctness={check}, {elapsed:.3f}s", flush=True)
    print(f"Results: {run_dir}")
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
