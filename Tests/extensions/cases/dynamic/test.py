import gc
import os
import sys


sys.path.insert(0, os.environ['CXXPYTHON_EXTENSION_FIXTURE'])
import _example
import _example as again


assert _example is again
assert _example.answer() == 42
assert _example.add(20, 22) == 42
assert _example.poll_wait() == 42
print('dynamic module: 42 42 42')

try:
    _example.fail()
    assert False
except ValueError:
    print('dynamic exception: ValueError')

saved = _example.answer
_example.value = 17
del sys.modules['_example']
del _example, again
gc.collect()
assert saved() == 17
import _example
assert _example.answer() == 42
print('retained binding:', saved(), _example.answer())
