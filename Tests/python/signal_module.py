import signal
import sys

print(signal.__doc__.__contains__('asynchronous events'))
print(type(signal.SIG_DFL))
print(type(signal.SIG_IGN))
print(type(signal.SIGINT))
print(sys.builtin_module_names.__contains__('signal'))
print(dir(signal).__contains__('signal'))
print(dir(signal).__contains__('getsignal'))
print(dir(signal).__contains__('raise_signal'))
print(signal.signal.__text_signature__)
print(signal.getsignal.__text_signature__)

seen = []

def handler(signum, frame):
    seen.append(signum == signal.SIGUSR1)
    seen.append(frame.__class__.__name__)

previous = signal.signal(signal.SIGUSR1, handler)
print(previous == signal.SIG_DFL)
print(signal.getsignal(signal.SIGUSR1) is handler)
print(signal.raise_signal(signal.SIGUSR1))
print(seen[0])
print(seen[1])
print(signal.signal(signal.SIGUSR1, signal.SIG_DFL) is handler)

try:
    signal.signal(signal.SIGUSR1, 42)
except TypeError as exc:
    print(type(exc))

try:
    signal.getsignal(99999)
except ValueError as exc:
    print(type(exc))
