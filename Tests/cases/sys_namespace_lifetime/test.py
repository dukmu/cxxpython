import builtins
import gc
import io
import sys as original_sys

streams = original_sys.stdin, original_sys.stdout, original_sys.stderr
hooks = (original_sys.displayhook, original_sys.excepthook,
         original_sys.unraisablehook)
missing = object()
last = getattr(builtins, "_", missing)
module_getattr = getattr(original_sys, "__getattr__", missing)
modules = original_sys.modules
del modules["sys"]
import sys as replacement

fresh = replacement is not original_sys, not hasattr(replacement, "stdout")
replacement_hooks = (replacement.displayhook, replacement.excepthook,
                     replacement.unraisablehook)


def wrong(*args):
    raise AssertionError("replacement sys must not supply streams or hooks")


class WrongStream:
    write = wrong
    flush = wrong
    readline = wrong


replacement.stdin = replacement.stdout = replacement.stderr = WrongStream()
replacement.displayhook = replacement.excepthook = replacement.unraisablehook = wrong
output = io.StringIO()
errors = io.StringIO()
original_sys.stdin = io.StringIO("answer\n")
original_sys.stdout = output
original_sys.stderr = errors
gc.collect()
print("original print")
answer = input("original prompt: ")
original_sys.__displayhook__("original display")
replacement_hooks[0]("replacement display")
displayed = []
original_sys.displayhook = lambda value: displayed.append(value)
exec(compile("17", "<sys lifetime>", "single"))
original_sys.__excepthook__(ValueError, ValueError("original exception"), None)
replacement_hooks[1](ValueError, ValueError("replacement exception"), None)

unraisable = []
original_sys.unraisablehook = lambda value: unraisable.append(value)


class Cleanup:
    def __del__(self):
        raise ValueError("cleanup")


obj = Cleanup()
obj.cycle = obj
del obj
gc.collect()
args_type = type(unraisable[0])
args = args_type((ValueError, ValueError("default unraisable"), None, None, None))
original_sys.__unraisablehook__(args)
replacement_hooks[2](args)

lookups = []
original_sys.__getattr__ = lambda name: lookups.append(name) or output
lost = []
del original_sys.stdin
try:
    input()
except RuntimeError as exc:
    lost.append(str(exc))
original_sys.stdin = streams[0]
del original_sys.stdout
try:
    original_sys.__displayhook__(1)
except RuntimeError as exc:
    lost.append(str(exc))
original_sys.stdout = output
if module_getattr is missing:
    del original_sys.__getattr__
else:
    original_sys.__getattr__ = module_getattr

original_sys.stdin, original_sys.stdout, original_sys.stderr = streams
original_sys.displayhook = hooks[0]
original_sys.unraisablehook = hooks[2]
if last is missing:
    del builtins._
else:
    builtins._ = last
print("fresh", fresh)
print("input", answer)
print("stdout", repr(output.getvalue()))
print("display", displayed)
print("stderr", repr(errors.getvalue()))
print("unraisable", len(unraisable), str(unraisable[0].exc_value))
print("missing", lost, lookups)


def final_hook(exc_type, exc_value, traceback):
    modules["sys"] = original_sys
    original_sys.excepthook = hooks[1]
    print("excepthook", exc_type.__name__, str(exc_value), file=streams[1])


original_sys.excepthook = final_hook
raise ValueError("terminal")
