# CPython 3.12.14 range_reduce/slice_reduce reconstruction contracts.
import copy
import gc


def failure(kind, action):
    try:
        action()
    except kind:
        return
    raise AssertionError('expected ' + kind.__name__)


for original in (range(0), range(4), range(10, -4, -3),
                 range(2**100, 2**101, 2**80)):
    constructor, arguments = original.__reduce__()
    assert constructor is range
    assert arguments == (original.start, original.stop, original.step)
    assert constructor(*arguments) == original
    assert original.__reduce__(1, 'ignored') == (constructor, arguments)
    for protocol in range(6):
        assert original.__reduce_ex__(protocol) == (constructor, arguments)
    assert copy.copy(original) is original
    assert copy.deepcopy(original) is original
failure(TypeError, lambda: range.__reduce__([]))
failure(TypeError, lambda: range(4).__reduce__(key=1))
print('range reconstruction ok')

for original in (slice(None), slice(1, 8, 2), slice(2**100, -(2**80), -1)):
    constructor, arguments = original.__reduce__()
    assert constructor is slice
    assert arguments == (original.start, original.stop, original.step)
    assert constructor(*arguments) == original
    for protocol in range(6):
        assert original.__reduce_ex__(protocol) == (constructor, arguments)
    assert copy.copy(original) is original
    result = copy.deepcopy(original)
    assert type(result) is slice and result == original
failure(TypeError, lambda: slice.__reduce__([]))
failure(TypeError, lambda: slice(4).__reduce__(1))
print('slice reconstruction ok')

shared = [1, 2]
original = slice(shared, shared, None)
constructor, arguments = original.__reduce__()
assert arguments[0] is arguments[1] is shared
assert constructor(*arguments).start is shared
result = copy.deepcopy(original)
assert result.start is result.stop and result.start is not shared
assert result.start == shared
print('slice aliases ok')

events = []


class Marker:
    def __index__(self):
        raise AssertionError('reduction must not convert slice fields')

    def __deepcopy__(self, memo):
        gc.collect()
        events.append('copy')
        return Marker()


marker = Marker()
original = slice(marker, marker, marker)
assert original.__reduce__()[1] == (marker, marker, marker)
assert events == []
result = copy.deepcopy(original)
assert result.start is result.stop is result.step
assert result.start is not marker and events == ['copy']
print('slice collecting callbacks ok')
