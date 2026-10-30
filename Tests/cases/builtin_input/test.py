import gc
import io
import sys

original = sys.stdin, sys.stdout, sys.stderr
events = []


class Reader:
    def __init__(self, value):
        self.value = value

    def readline(self):
        events.append("readline")
        gc.collect()
        return self.value


class Writer:
    def __init__(self, label):
        self.label = label

    def flush(self):
        events.append(self.label + ".flush")
        gc.collect()
        raise ValueError("ignored")

    def write(self, value):
        events.append(self.label + ".write:" + value)
        gc.collect()


class Prompt:
    def __str__(self):
        events.append("prompt.str")
        gc.collect()
        return "question? "


sys.stdout = Writer("out")
sys.stderr = Writer("err")
sys.stdin = Reader("hello\r\n")
answer = input(Prompt())
sys.stdin, sys.stdout, sys.stderr = original
print(repr(answer), events)

for value in ("plain", "\n", b"bytes\n", b"\r\n", b"", "", 42):
    sys.stdin = Reader(value)
    sys.stdout = Writer("out")
    sys.stderr = Writer("err")
    try:
        result = repr(input())
    except Exception as exc:
        result = type(exc).__name__ + ": " + str(exc)
    sys.stdin, sys.stdout, sys.stderr = original
    print(repr(value), result)

sys.stdin = io.StringIO("first\nsecond")
sys.stdout = io.StringIO()
print_result = (input("one: "), input(), sys.stdout.getvalue())
sys.stdin, sys.stdout, sys.stderr = original
print(print_result)
