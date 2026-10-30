import gc
import sys


names = ("ljust", "rjust", "center", "zfill")

for text in ("", "a", "ab", "abc", "\u4e2d", "\U0001f642", "\ud800", "\0"):
    for width in range(-2, 9):
        for name in names:
            result = getattr(text, name)(width)
            assert type(result) is str
            assert len(result) == max(len(text), width)
            if width <= len(text):
                assert result is text
            if name == "ljust":
                assert result == text + " " * max(0, width - len(text))
            elif name == "rjust":
                assert result == " " * max(0, width - len(text)) + text
            elif name == "center":
                margin = max(0, width - len(text))
                left = margin // 2 + (margin & width & 1)
                assert result == " " * left + text + " " * (margin - left)
            else:
                assert result == "0" * max(0, width - len(text)) + text

for name in names[:-1]:
    for fill in (".", "\0", "\u4e2d", "\U0001f642", "\ud800"):
        result = getattr("xy", name)(5, fill)
        assert len(result) == 5
        print(name, repr(fill), repr(result))

for text in ("", "+", "-", "+12", "-12", "12", " 12", "\u221212", "\0-1"):
    print("zfill", repr(text), repr(text.zfill(6)))


class Text(str):
    def __str__(self):
        raise AssertionError("padding must not call __str__")

    def __len__(self):
        raise AssertionError("padding must not call __len__")

    def __getitem__(self, item):
        raise AssertionError("padding must not call __getitem__")


class WidthInt(int):
    def __index__(self):
        raise AssertionError("integer subclass index override must be ignored")


for name in names:
    text = Text("abc")
    for width in (-sys.maxsize - 1, -1, 0, 3, 6, False, True, WidthInt(6)):
        result = getattr(text, name)(width)
        assert type(result) is str
        assert result == getattr("abc", name)(int(width))
    if name != "zfill":
        assert getattr(text, name)(5, Text(".")) == getattr("abc", name)(5, ".")
print("identity and subclasses ok")


events = []


class Width:
    def __init__(self, value):
        self.value = value

    def __index__(self):
        events.append("index")
        gc.collect()
        return self.value


class IndexFailure:
    def __index__(self):
        events.append("failure")
        gc.collect()
        raise LookupError("index failed")


class OnlyInt:
    def __int__(self):
        raise AssertionError("width must use __index__, not __int__")


def failure(function, expected, show=False):
    try:
        function()
    except expected as error:
        if show:
            print(type(error).__name__, str(error))
    else:
        raise AssertionError("expected " + expected.__name__)


for name in names:
    events.clear()
    result = getattr(Text("xy"), name)(Width(6))
    assert result == getattr("xy", name)(6)
    assert events == ["index"]
    for value in (1.5, "6", None, OnlyInt()):
        failure(lambda: getattr("xy", name)(value), TypeError)
    failure(lambda: getattr("xy", name)(Width("6")), TypeError)
    for width in (sys.maxsize + 1, -sys.maxsize - 2):
        failure(lambda: getattr("xy", name)(width), OverflowError, True)
    failure(lambda: getattr("xy", name)(width=6), TypeError)
    failure(lambda: getattr(str, name)(123, Width(6)), TypeError)
    if name == "zfill":
        failure(lambda: "xy".zfill(6, "0"), TypeError)
        continue
    events.clear()
    result = getattr(Text("xy"), name)(Width(6), Text("\ud800"))
    assert result == getattr("xy", name)(6, "\ud800")
    assert events == ["index"]
    for fill in ("", "ab", "\U0001f642\u0301", b".", None, 7):
        events.clear()
        failure(lambda: getattr("xy", name)(Width(0), fill), TypeError, True)
        assert events == ["index"]
    events.clear()
    failure(lambda: getattr("xy", name)(IndexFailure(), ""), LookupError, True)
    assert events == ["failure"]
    events.clear()
    failure(lambda: getattr("xy", name)(Width(sys.maxsize + 1), ""), OverflowError)
    assert events == ["index"]
    events.clear()
    failure(lambda: getattr(str, name)(123, Width(6), "."), TypeError)
    assert events == []
    failure(lambda: getattr("xy", name)(6, fillchar="."), TypeError)
print("width, fill, callback order and GC ok")
