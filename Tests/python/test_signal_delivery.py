"""Exercise asynchronous process signals at VM and native-wait boundaries."""
import argparse
import os
from pathlib import Path
import selectors
import signal
import subprocess
import time


CASES = (
    ("cpu", """
import _signal, gc, sys
events = []
def handler(number, frame):
    gc.collect()
    events.append(frame.f_code.co_name)
_signal.signal(_signal.SIGUSR1, handler)
gc.collect()
print('ready', flush=True)
while not events:
    pass
print('delivered', events)
""", signal.SIGUSR1, b"delivered ['<module>']\n"),
    ("sleep", """
import _signal, time
try:
    print('ready', flush=True)
    time.sleep(10)
except KeyboardInterrupt:
    print('interrupted')
""", signal.SIGINT, b"interrupted\n"),
    ("select", """
import _signal, select
try:
    print('ready', flush=True)
    select.select([], [], [], 10)
except KeyboardInterrupt:
    print('interrupted')
""", signal.SIGINT, b"interrupted\n"),
    ("socket", """
import _signal, socket
left, right = socket.socketpair()
try:
    print('ready', flush=True)
    left.recv(1)
except KeyboardInterrupt:
    print('interrupted')
finally:
    left.close()
    right.close()
""", signal.SIGINT, b"interrupted\n"),
)


def run(binary, source, number, waiting=None):
    process = subprocess.Popen([str(binary), '-c', source], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            if not selector.select(10):
                raise AssertionError('interpreter did not publish readiness')
            assert process.stdout.readline() == b'ready\n'
        if waiting:
            deadline = time.monotonic() + 5
            while True:
                channel = Path(f'/proc/{process.pid}/wchan').read_text().strip()
                blocked = (channel == 'hrtimer_nanosleep' if waiting == 'sleep' else
                           any(name in channel for name in
                               ('poll', 'select', 'sock', 'skb', 'unix_stream')))
                if waiting == 'socket' and channel == '0':
                    # Some Linux kernels hide the socket wait symbol. The actual
                    # recv/poll syscall still proves the child has left Python.
                    calls = {'x86_64': (45, 47, 7, 271), 'aarch64': (207, 212, 73)}
                    syscall = Path(f'/proc/{process.pid}/syscall').read_text().split()[0]
                    blocked = (syscall != 'running' and
                               int(syscall) in calls.get(os.uname().machine, ()))
                if blocked:
                    break
                if time.monotonic() >= deadline or process.poll() is not None:
                    raise AssertionError(f'interpreter did not enter native {waiting}: {channel}')
                time.sleep(0.001)
        process.send_signal(number)
        stdout, stderr = process.communicate(timeout=10)
        assert process.returncode == 0, (process.returncode, stdout, stderr)
        assert stderr == b'', stderr
        return stdout
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    options = parser.parse_args()
    for name, source, number, expected in CASES:
        waiting = name if name != 'cpu' else None
        assert run(options.reference.resolve(), source, number, waiting) == expected
        assert run(options.binary.resolve(), source, number, waiting) == expected
        print('PASS', name)
    # Interpreter-owned tracing state intentionally outlives module-table removal.
    source = CASES[0][1].replace("gc.collect()\nprint('ready'",
                               "del handler\ndel sys.modules['_signal']\ndel _signal\n"
                               "gc.collect()\nprint('ready'")
    assert run(options.binary.resolve(), source, signal.SIGUSR1) == CASES[0][3]
    print('PASS', 'owned-after-module-removal')
