import _signal
import gc
import signal
import sys
import threading

main = threading.get_ident()
events = []
errors = []

def handler(number, frame):
    gc.collect()
    events.append((number == signal.SIGUSR1, threading.get_ident() == main,
                   frame is not None and isinstance(frame.f_code.co_name, str)))

original = signal.signal(signal.SIGUSR1, handler)
try:
    assert signal.getsignal(signal.SIGUSR1) is handler
    signal.raise_signal(signal.SIGUSR1)
    print('direct', events)

    def worker():
        try:
            signal.signal(signal.SIGUSR1, handler)
        except ValueError:
            errors.append('main-only')
        signal.raise_signal(signal.SIGUSR1)

    thread = threading.Thread(target=worker)
    thread.start()
    thread.join(5)
    assert not thread.is_alive()
    for _ in range(100000):
        if len(events) == 2:
            break
    assert len(events) == 2
    print('worker notification', events[-1], errors)
    print('signal enums', signal.Signals(signal.SIGINT).name,
          signal.Handlers(signal.SIG_IGN).name, signal.SIGINT in signal.valid_signals())
    print('signal zero', signal.raise_signal(0))
    try:
        signal.default_int_handler(signal.SIGINT, None)
    except KeyboardInterrupt as error:
        print('keyboard interrupt', error.args)
finally:
    signal.signal(signal.SIGUSR1, original)

for number in (0, -1, signal.NSIG):
    try:
        signal.getsignal(number)
    except ValueError:
        print('invalid signal rejected')
try:
    signal.signal(signal.SIGUSR1, object())
except TypeError:
    print('invalid handler rejected')
