import sys


def describe(exc):
    if isinstance(exc, BaseExceptionGroup):
        return (type(exc).__name__, exc.message, [describe(e) for e in exc.exceptions])
    return (type(exc).__name__, str(exc))


class Tagged(ExceptionGroup):
    def __new__(cls, message, exceptions, tag):
        obj = super().__new__(cls, message, exceptions)
        obj.tag = tag
        return obj

    def derive(self, exceptions):
        return Tagged(self.message, exceptions, self.tag)


original = Tagged("root", [ValueError("v"), ExceptionGroup("inner", [TypeError("t"), ValueError("w")]), KeyError("k")], "tag")
original.add_note("root note")
original.__cause__ = LookupError("cause")
original.__context__ = OSError("context")
try:
    try:
        raise original
    except* ValueError as part:
        print("values", describe(part), part.tag, part.__notes__)
        print("metadata", part.__cause__ is original.__cause__, part.__context__ is original.__context__, part.__traceback__ is original.__traceback__)
        print("active", sys.exception() is part, sys.exc_info()[1] is part)
        raise RuntimeError("new") from part
    except* TypeError as part:
        print("types", describe(part), part.tag)
        raise
    finally:
        print("finally", sys.exception() is not None)
except BaseException as result:
    print("combined", describe(result))
    new, remainder = result.exceptions
    print("new metadata", type(new.__cause__).__name__, new.__suppress_context__, new.__context__ is new.__cause__)
    print("remainder metadata", remainder.tag, remainder.__notes__, remainder.__cause__ is original.__cause__, remainder.__traceback__ is original.__traceback__)
print("cleared", sys.exception(), sys.exc_info()[1])
try:
    print(part)
except NameError:
    print("alias deleted")

try:
    raise ValueError("naked")
except* ValueError as part:
    print("wrapped", describe(part))

try:
    try:
        raise ExceptionGroup("outer", [ValueError("a"), TypeError("b")])
    except* ValueError:
        try:
            raise ExceptionGroup("nested", [KeyError("c")])
        except* KeyError as nested:
            print("nested", describe(nested))
        else:
            print("unexpected nested else")
        finally:
            print("nested finally")
    except* TypeError:
        print("outer type")
    else:
        print("unexpected outer else")
    finally:
        print("outer finally")
except BaseException as exc:
    print("unexpected", describe(exc))

try:
    pass
except* ValueError:
    print("unexpected handler")
else:
    print("normal else")
finally:
    print("normal finally")

try:
    try:
        raise ExceptionGroup("original", [ValueError("x"), TypeError("y")])
    except* ValueError as part:
        part.add_note("changed")
        part.__cause__ = RuntimeError("changed cause")
        part.__traceback__ = None
        raise
except BaseException as result:
    print("changed reraised", describe(result))
    print("changed metadata", result.exceptions[0].__notes__, type(result.exceptions[0].__cause__).__name__, result.exceptions[0].__traceback__ is None)
