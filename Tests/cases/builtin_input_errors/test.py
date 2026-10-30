import sys

original = sys.stdin, sys.stdout, sys.stderr


class Reader:
    def readline(self):
        raise ValueError("read failed")


class Writer:
    def flush(self):
        pass

    def write(self, value):
        raise ValueError("write failed")


class BadPrompt:
    def __str__(self):
        raise ValueError("str failed")


class NoWrite:
    def flush(self):
        pass


class BadFileno(Reader):
    def fileno(self):
        return "invalid"


class IgnoredFileno(Reader):
    def fileno(self):
        raise ValueError("ignored")


def run(label, function, message=True):
    try:
        function()
    except Exception as exc:
        result = type(exc).__name__
        if message:
            result += ": " + str(exc)
    else:
        result = "no error"
    sys.stdin, sys.stdout, sys.stderr = original
    print(label, result)


for name in ("stdin", "stdout", "stderr"):
    setattr(sys, name, None)
    run(name + " none", input)
    delattr(sys, name)
    run(name + " missing", input)

sys.stdin = Reader()
sys.stdout = Writer()
run("write", lambda: input("x"))
sys.stdin = Reader()
sys.stdout = Writer()
run("str", lambda: input(BadPrompt()))
sys.stdin = Reader()
sys.stdout = NoWrite()
run("missing write first", lambda: input(BadPrompt()))
sys.stdin = Reader()
run("read", input)
sys.stdin = BadFileno()
run("fileno conversion", input)
sys.stdin = IgnoredFileno()
run("fileno failure", input)
run("two arguments", lambda: input(1, 2), False)
run("keyword", lambda: input(prompt="x"), False)
