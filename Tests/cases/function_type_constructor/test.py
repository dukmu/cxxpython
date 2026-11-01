import builtins
import gc
import threading
from types import FunctionType


def original(value=2):
    """doc"""
    return value + offset


globals_dict = {'offset': 40, '__name__': 'example'}
f = FunctionType(original.__code__, globals_dict, 'renamed', (2,))
print(f(), f.__name__, f.__qualname__, f.__module__, f.__doc__)
print(f.__globals__ is globals_dict, f.__defaults__, f.__closure__)
g = FunctionType(code=original.__code__, globals=globals_dict, argdefs=(3,))
print(g(), g.__name__, g.__builtins__ is __builtins__.__dict__)


def outer(value):
    def inner(amount):
        return value + amount
    return inner


source = outer(40)
h = FunctionType(source.__code__, {}, closure=source.__closure__)
print(h(2), h.__closure__ is source.__closure__)
empty = FunctionType(original.__code__, globals_dict, closure=())
print(empty.__closure__ == ())


def lookup():
    return answer


class Mapping:
    def __getitem__(self, key):
        if key == 'answer':
            return 42
        raise KeyError(key)


mapping = Mapping()
for supplied in ({'answer': 42}, mapping, builtins):
    function = FunctionType(lookup.__code__, {'__builtins__': supplied})
    print(function.__builtins__ is (builtins.__dict__ if supplied is builtins else supplied))
    if supplied is not builtins:
        print(function())
function = FunctionType(original.__code__, {'__builtins__': None})
print(function.__builtins__ is None)
for invalid in (None, 42):
    function = FunctionType(lookup.__code__, {'__builtins__': invalid})
    print(function.__builtins__ is invalid)
    try:
        function()
    except TypeError:
        print('not a mapping')
globals_with_builtins = {'__builtins__': {'answer': 42}}
function = FunctionType(lookup.__code__, globals_with_builtins)
globals_with_builtins['__builtins__'] = {'answer': 99}
print(function(), function.__builtins__ is globals_with_builtins['__builtins__'])


def make_nested():
    return FunctionType(lookup.__code__, {})


caller_globals = {'FunctionType': FunctionType, 'lookup': lookup,
                  '__builtins__': mapping}
caller = FunctionType(make_nested.__code__, caller_globals)
print(caller().__builtins__ is mapping, caller()())


def failure(*args, **kwargs):
    try:
        FunctionType(*args, **kwargs)
    except Exception as exc:
        print(type(exc).__name__)


failure()
failure(1, {})
failure(original.__code__, [])
failure(original.__code__, {}, 1)
failure(original.__code__, {}, None, [])
failure(source.__code__, {})
failure(source.__code__, {}, closure=1)
failure(source.__code__, {}, closure=())
failure(source.__code__, {}, closure=(1,))
failure(original.__code__, {}, closure=(source.__closure__[0],))
failure(original.__code__, {}, unexpected=1)
failure(original.__code__, {}, code=original.__code__)

for args in ((1, {}), (original.__code__, []),
             (source.__code__, {}, None, None, (1,)),
             (source.__code__, {}, None, None, ())):
    try:
        FunctionType(*args)
    except Exception as exc:
        print(str(exc))


def retained(value):
    def body(delta=2):
        return value + delta + bonus
    return FunctionType(body.__code__, {'bonus': 0}, 'retained', (2,), body.__closure__)


function = retained(40)
gc.collect()
print('gc roots', function() == 42, function.__name__ == 'retained')

errors = []
results = []


def worker():
    try:
        for i in range(8):
            function = retained(40)
            if i % 4 == 0:
                gc.collect()
            assert function() == 42
        results.append(True)
    except BaseException as exc:
        errors.append(type(exc).__name__)


threads = [threading.Thread(target=worker) for _ in range(4)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
print('parallel construction', errors == [], len(results) == 4)
