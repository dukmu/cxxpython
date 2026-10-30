"""Execute script fixtures with byte-exact output checks, without native tests."""

import argparse
import difflib
from pathlib import Path
import subprocess
import sys
import tempfile


def compare(name, expected, actual):
    if actual == expected:
        return True
    print(f"{name} differs:", flush=True)
    diff = difflib.diff_bytes(difflib.unified_diff, expected.splitlines(keepends=True),
                             actual.splitlines(keepends=True), b"expected", b"actual")
    sys.stdout.flush()
    sys.stdout.buffer.writelines(diff)
    if not actual.endswith(b"\n"):
        print("\n(actual has no final newline)")
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--cases", type=Path, default=Path(__file__).resolve().parents[1] / "cases")
    parser.add_argument("--case", action="append",
                        help="Run a fixture directory without rebuilding; repeat to select several")
    args = parser.parse_args()
    binary = args.binary.resolve()
    root = args.cases.resolve()
    scripts = (sorted(root.glob("*/test.py")) if args.case is None else
               [root / name / "test.py" for name in args.case])
    if not scripts:
        parser.error("no script cases found")
    failures = 0
    for script in scripts:
        directory = script.parent
        input_path = directory / "stdin.bin"
        stdin = input_path.read_bytes() if input_path.exists() else b""
        expected = (directory / "stdout.bin").read_bytes()
        error_path = directory / "stderr.bin"
        stderr = error_path.read_bytes() if error_path.exists() else b""
        status_path = directory / "exitcode.txt"
        status = int(status_path.read_text()) if status_path.exists() else 0
        try:
            with tempfile.TemporaryDirectory(prefix=f'cxxpython-{directory.name}-') as scratch:
                result = subprocess.run([str(binary), str(script)], input=stdin,
                                        capture_output=True, cwd=scratch, timeout=30)
        except subprocess.TimeoutExpired:
            print(f"FAIL {directory.name}: exceeded 30 seconds", flush=True)
            failures += 1
            continue
        ok = compare(f"{directory.name}/stdout", expected, result.stdout)
        ok = compare(f"{directory.name}/stderr", stderr, result.stderr) and ok
        if result.returncode != status:
            print(f"{directory.name}: exit {result.returncode}, expected {status}")
            ok = False
        print(f"{'PASS' if ok else 'FAIL'} {directory.name}", flush=True)
        failures += not ok
    print(f"{len(scripts) - failures}/{len(scripts)} script cases passed")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
