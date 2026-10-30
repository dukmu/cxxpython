"""Exercise GNU readline with a real pseudo-terminal after integration."""

import argparse
import os
from pathlib import Path
import pty
import select
import subprocess
import termios
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True, type=Path)
    args = parser.parse_args()
    script = Path(__file__).resolve().parents[1] / 'cases/readline_interactive/test.py'
    master, slave = pty.openpty()
    original_termios = termios.tcgetattr(slave)
    env = os.environ.copy()
    env['INPUTRC'] = '/dev/null'
    process = subprocess.Popen([str(args.binary.resolve()), str(script)],
                               stdin=slave, stdout=slave, stderr=slave, close_fds=True,
                               env=env)
    output = bytearray()
    stage = 0
    deadline = time.monotonic() + 15
    try:
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.1)
            if ready:
                try:
                    chunk = os.read(master, 4096)
                except OSError:
                    break
                if not chunk:
                    break
                output.extend(chunk)
                if stage == 0 and b'prompt> ' in output:
                    os.write(master, b'\x15al\t\n')
                    stage = 1
                if stage == 1 and b'again> ' in output:
                    os.write(master, b'\x1b[A\n')
                    stage = 2
            if process.poll() is not None and not ready:
                break
        if process.poll() is None:
            process.kill()
        status = process.wait()
        restored = termios.tcgetattr(slave) == original_termios
    finally:
        os.close(master)
        os.close(slave)
    if (stage != 2 or status != 0 or not restored or output.count(b'HOOK') != 1 or
            b'RESULT alpha' not in output or b'HISTORY alpha' not in output):
        print(f'FAIL: status={status} stage={stage} restored={restored} output={bytes(output)!r}')
        return 1
    print('PASS readline PTY editing, completion, history and terminal restoration')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
