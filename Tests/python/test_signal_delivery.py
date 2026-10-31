"""Exercise asynchronous process signals at VM and native-sleep boundaries."""
import argparse
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
)


def run(binary, source, number, sleeping=False):
    process = subprocess.Popen([str(binary), '-c', source], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            if not selector.select(10):
                raise AssertionError('interpreter did not publish readiness')
            assert process.stdout.readline() == b'ready\n'
        if sleeping:
            deadline = time.monotonic() + 5
            while (Path(f'/proc/{process.pid}/wchan').read_text().strip() != 'hrtimer_nanosleep'):
                if time.monotonic() >= deadline or process.poll() is not None:
                    raise AssertionError('interpreter did not enter native sleep')
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
        assert run(options.reference.resolve(), source, number, name == 'sleep') == expected
        assert run(options.binary.resolve(), source, number, name == 'sleep') == expected
        print('PASS', name)
    # Interpreter-owned tracing state intentionally outlives module-table removal.
    source = CASES[0][1].replace("gc.collect()\nprint('ready'",
                               "del handler\ndel sys.modules['_signal']\ndel _signal\n"
                               "gc.collect()\nprint('ready'")
    assert run(options.binary.resolve(), source, signal.SIGUSR1) == CASES[0][3]
    print('PASS', 'owned-after-module-removal')
