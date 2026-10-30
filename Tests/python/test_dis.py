"""Run product-specific VM disassembly assertions without CPython bytecode comparisons."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    script = Path(__file__).resolve().parents[1] / "vm_cases/dis_vm/test.py"
    result = subprocess.run([str(args.binary.resolve()), str(script)], capture_output=True)
    if result.returncode or result.stdout or result.stderr:
        raise SystemExit(f"dis VM test failed: {result.returncode}\n{result.stdout!r}\n{result.stderr!r}")


if __name__ == "__main__":
    main()
