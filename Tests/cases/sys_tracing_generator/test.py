import sys

events = []


def trace(frame, event, arg):
    if frame.f_code.co_name == "trace":
        events.append("recursive")
    if frame.f_code.co_name == "generator" and event in ("call", "return"):
        events.append((event, arg))
    return trace


def generator():
    yield 1
    return 2


sys.settrace(trace)
it = generator()
print(next(it))
try:
    next(it)
except StopIteration as error:
    print(error.value)
sys.settrace(None)
print(events)
