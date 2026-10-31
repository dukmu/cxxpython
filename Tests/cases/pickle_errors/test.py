import io
import pickle


class Broken:
    def __reduce__(self):
        return 123


class CallbackFailure:
    def __reduce_ex__(self, protocol):
        raise RuntimeError('reducer failure')


def check(label, expected, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except expected as exc:
        print(label, type(exc).__name__)
    else:
        raise AssertionError(label)


check('protocol', ValueError, pickle.dumps, None, protocol=6)
check('callback protocol', ValueError, pickle.dumps, None, protocol=4,
      buffer_callback=lambda value: False)
check('empty', EOFError, pickle.loads, b'')
check('load protocol', ValueError, pickle.loads, b'\x80\x06.')
check('unicode input', TypeError, pickle.loads, 'N.')
# Both upstream backends reject text streams, with different diagnostics.
try:
    pickle.load(io.StringIO('N.'))
except (TypeError, AssertionError):
    print('text file rejected', True)
else:
    raise AssertionError('text file accepted')
check('reduce result', pickle.PicklingError, pickle.dumps, Broken())
check('callback exception', RuntimeError, pickle.dumps, CallbackFailure())
check('lambda', pickle.PicklingError, pickle.dumps, lambda: None)
check('memoryview', TypeError, pickle.dumps, memoryview(b'abc'))
assert issubclass(pickle.PicklingError, pickle.PickleError)
assert issubclass(pickle.UnpicklingError, pickle.PickleError)
print('exception hierarchy', True)
