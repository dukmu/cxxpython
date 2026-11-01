"""Exercise unmodified dill; optionally pass its installation directory as argv[1]."""

import os
import sys

if len(sys.argv) > 1:
    sys.path.insert(0, sys.argv[1])
else:
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
    sys.path.insert(0, os.path.join(root, '.cache', 'deps', 'dill-0.4.1-wheel'))

import array
import dill
from types import FunctionType

assert dill.__version__ == '0.4.1'
print('dill', dill.__version__)

for value in (None, True, 1 << 100, 1.25, 1 + 2j, 'value λ', b'bytes',
              [1, 2.5, 'value'], {'key': (1, 2)}, {1, 3}, frozenset({2, 4})):
    clone = dill.loads(dill.dumps(value))
    assert clone == value and type(clone) is type(value)
print('values ok')

cycle = []
shared = [cycle, cycle]
cycle.append(shared)
clone = dill.loads(dill.dumps(shared))
assert clone[0] is clone[1] and clone[0][0] is clone
print('object graph ok')


def calculation(value, offset=2, *, scale=3):
    return (value + offset) * scale


code = dill.loads(dill.dumps(calculation.__code__))
assert type(code) is type(calculation.__code__)
assert code.co_name == calculation.__code__.co_name
assert code.co_argcount == 2 and code.co_kwonlyargcount == 1
restored = FunctionType(code, globals(), 'restored', (2,))
restored.__kwdefaults__ = {'scale': 3}
assert restored(12) == 42
print('code ok')

clone = dill.loads(dill.dumps(calculation))
assert clone(12) == 42 and clone(5, 2, scale=6) == 42
print('function ok')


def closure(offset):
    return lambda value: value + offset


clone = dill.loads(dill.dumps(closure(40)))
assert clone(2) == 42
print('closure ok')


def factorial(value):
    return 1 if value <= 1 else value * factorial(value - 1)


clone = dill.loads(dill.dumps(factorial, recurse=True))
assert clone(6) == 720
print('recursive globals ok')


class Example:
    def __init__(self, value):
        self.value = value

    def read(self):
        return self.value


clone = dill.loads(dill.dumps(Example(42)))
assert clone.read() == 42
print('class ok')
method = dill.loads(dill.dumps(Example(7).read))
assert method() == 7
print('bound method ok')

original = array.array('i', [1, -2, 3])
clone = dill.loads(dill.dumps(original))
assert type(clone) is array.array and clone == original
print('array ok')
