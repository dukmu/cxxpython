"""Compare curses wrapper behavior and terminal restoration on a PTY."""

import argparse
import os
from pathlib import Path
import pty
import select
import subprocess
import termios
import time


def run(binary, script, input_gc=False, term="xterm"):
    master, slave = pty.openpty()
    before = termios.tcgetattr(slave)
    env = os.environ.copy()
    env["TERM"] = term
    if term != "xterm":
        env["CURSES_FAILURE_CASE"] = "1"
    process = subprocess.Popen([str(binary.resolve()), str(script)], stdin=slave,
                               stdout=slave, stderr=slave, env=env, close_fds=True)
    output = bytearray()
    sent = False
    deadline = time.monotonic() + 15
    try:
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.1)
            if ready:
                try:
                    output.extend(os.read(master, 4096))
                    if input_gc and not sent and b"CURSES_GC_PROGRESS" in output:
                        os.write(master, b"z")
                        sent = True
                except OSError:
                    break
            if process.poll() is not None and not ready:
                break
        if process.poll() is None:
            process.kill()
        code = process.wait()
        restored = termios.tcgetattr(slave) == before
    finally:
        os.close(master)
        os.close(slave)
    marker = (b"CURSES_INIT_ERROR" if term != "xterm" else
              b"CURSES_INPUT_DONE" if input_gc else b"CURSES_RESTORED")
    if (code != 0 or not restored or marker not in output or
            (input_gc and (not sent or b"CURSES_GETCH_READY" not in output))):
        raise AssertionError(f"{binary}: status={code}, restored={restored}, output={bytes(output)!r}")


def run_nonterminal(binary, script):
    env = os.environ.copy()
    env["TERM"] = "xterm"
    env["CURSES_FAILURE_CASE"] = "1"
    result = subprocess.run([str(binary.resolve()), str(script)], stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
                            timeout=15)
    if result.returncode != 0 or b"CURSES_INIT_ERROR" not in result.stdout:
        raise AssertionError(f"{binary}: nonterminal status={result.returncode}, "
                             f"stdout={result.stdout!r}, stderr={result.stderr!r}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--reference", type=Path)
    args = parser.parse_args()
    cases = Path(__file__).resolve().parents[1] / "pty_cases"
    wrapper = cases / "curses_wrapper/test.py"
    input_gc = cases / "curses_input_gc/test.py"
    for binary in (args.reference, args.binary):
        if binary is None:
            continue
        run(binary, wrapper)
        run(binary, input_gc, input_gc=True)
        run(binary, input_gc, term="cxxpython-unknown-terminal")
        run_nonterminal(binary, input_gc)
    print("PASS curses PTY restoration, input/GC progress, and initialization errors")


if __name__ == "__main__":
    main()
