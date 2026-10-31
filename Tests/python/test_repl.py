"""Terminal REPL and shared-heap progress while native editing waits for input."""

import argparse
import fcntl
import os
from pathlib import Path
import pty
import select
import subprocess
import struct
import termios
import tempfile
import time


def terminal(binary, arguments, responses, expected):
    master, slave = pty.openpty()
    initial = termios.tcgetattr(slave)
    process = subprocess.Popen([str(binary), *arguments], stdin=slave, stdout=slave,
                               stderr=slave, close_fds=True)
    output = bytearray()
    next_response = 0
    deadline = time.monotonic() + 20
    try:
        while time.monotonic() < deadline:
            readable, _, _ = select.select([master], [], [], 0.1)
            if readable:
                try:
                    chunk = os.read(master, 4096)
                except OSError:
                    break
                if not chunk:
                    break
                output.extend(chunk)
                if next_response < len(responses):
                    prompt, reply = responses[next_response]
                    if prompt in output:
                        os.write(master, reply)
                        next_response += 1
            if process.poll() is not None:
                break
        if process.poll() is None:
            raise AssertionError(f"terminal timed out: {bytes(output)!r}")
        assert process.wait() == 0, bytes(output)
        for fragment in expected:
            assert fragment in output, (fragment, bytes(output))
        final = termios.tcgetattr(slave)
        for flag in (termios.ICANON, termios.ECHO):
            assert final[3] & flag == initial[3] & flag, (initial, final)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        os.close(slave)


def backpressure(binary, script):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    initial = termios.tcgetattr(slave)
    with tempfile.TemporaryDirectory(prefix="cxxpython-readline-pressure-") as scratch:
        # This ASCII I/O test does not need GNU's multibyte prompt scanner.
        environment = dict(os.environ, LC_ALL="C")
        process = subprocess.Popen([str(binary), str(script)], cwd=scratch,
                                   stdin=slave, stdout=slave, stderr=slave, close_fds=True,
                                   env=environment)
        try:
            readable, _, _ = select.select([master], [], [], 10)
            assert readable, "long prompt never reached the terminal"
            Path(scratch, "collect_go").touch()
            deadline = time.monotonic() + 10
            while not Path(scratch, "collect_done").exists():
                status = process.poll()
                if status is not None:
                    chunk = os.read(master, 65536)
                    raise AssertionError(f"interpreter exit {status}: {chunk[-1000:]!r}")
                if time.monotonic() >= deadline:
                    raise AssertionError("GC blocked by unread terminal output")
                time.sleep(0.01)
            output = bytearray()
            sent = False
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                readable, _, _ = select.select([master], [], [], 0.1)
                if readable:
                    chunk = os.read(master, 65536)
                    if not chunk:
                        break
                    output.extend(chunk)
                    if not sent and b"ready> " in output:
                        os.write(master, b"answer\n")
                        sent = True
                if process.poll() is not None:
                    break
            assert process.wait(timeout=2) == 0, bytes(output[-256:])
            assert b"BACKPRESSURE_DONE" in output, bytes(output[-256:])
            assert termios.tcgetattr(slave) == initial
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            os.close(master)
            os.close(slave)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    root = Path(__file__).resolve().parents[1]
    terminal(binary, [str(root / "pty_cases/readline_gc/test.py")],
             [(b"GC_PROGRESS", b"answer\n")], [b"RESULT answer", b"THREAD_DONE"])
    terminal(binary, ["-q"], [(b">>> ", b"1 + 20\x7f\n\x04")], [b"3"])
    patterns = root / "pty_cases/repl_patterns"
    terminal(binary, ["-q", "-i", str(patterns / "test.py")],
             [(b">>> ", (patterns / "stdin.bin").read_bytes() + b"\x04")],
             [b"PAIR 3 4", b"\r\n7\r\n", b"SyntaxError", b"RECOVERED 7"])
    annotations = root / "pty_cases/repl_annotations"
    commands = (annotations / "stdin.bin").read_bytes() + b"\x04"
    expected = [b"{'value': 'Missing'}", b"{'arg': 'Unknown', 'return': 'list[int]'}",
                b"FUTURE True"]
    terminal(binary, ["-q", "-i", str(annotations / "test.py")],
             [(b">>> ", commands)], expected)
    terminal(binary, ["-q"],
             [(b">>> ", b"from __future__ import annotations\n" + commands)], expected)
    backpressure(binary, root / "pty_cases/readline_backpressure/test.py")
    print("PASS terminal REPL editing, GC progress and terminal restoration")


if __name__ == "__main__":
    main()
