# Behavioral cases adapted from CPython 3.12.14 Lib/test/test_copy.py (PSF license).
import copy
import weakref


def check(name, action):
    try:
        action()
    except Exception as exc:
        print(name, type(exc).__name__, str(exc))
    else:
        print(name, 'ok')


def atomic():
    def function():
        pass

    class Example:
        pass

    values = (None, Ellipsis, NotImplemented, 42, 2**100, 3.14, True, False,
              1j, 'hello\u1234', b'world', range(10), function,
              function.__code__, Example, max, property())
    for value in values:
        assert copy.copy(value) is value
        assert copy.deepcopy(value) is value
    assert copy.Error is copy.error
    assert issubclass(copy.Error, Exception)
    immutable_tuple = ((1, 2), 3)
    assert copy.copy(immutable_tuple) is immutable_tuple
    assert copy.deepcopy(immutable_tuple) is immutable_tuple
    original_slice = slice(1, 10, 2)
    assert copy.copy(original_slice) is original_slice


def lists_and_dicts():
    for original in ([[1, 2], 3], {'first': [1, 2], 'second': 3}):
        shallow = copy.copy(original)
        deep = copy.deepcopy(original)
        key = 0 if isinstance(original, list) else 'first'
        assert shallow == original == deep
        assert shallow is not original and deep is not original
        assert shallow[key] is original[key]
        assert deep[key] is not original[key]
    original = ([1, 2], 3)
    shallow = copy.copy(original)
    deep = copy.deepcopy(original)
    assert shallow is original
    assert deep == original and deep is not original
    assert deep[0] is not original[0]


def sets():
    original = {1, 2, 3}
    for operation in (copy.copy, copy.deepcopy):
        result = operation(original)
        assert result == original and result is not original


def frozensets():
    original = frozenset({1, 2, 3})
    assert copy.copy(original) is original
    result = copy.deepcopy(original)
    assert result == original and result is not original


def bytearrays():
    original = bytearray(b'abc')
    for operation in (copy.copy, copy.deepcopy):
        result = operation(original)
        assert result == original and result is not original
        result[0] = 100
        assert original == bytearray(b'abc')


def instances():
    class Example:
        def __init__(self, value):
            self.value = value

    original = Example([42])
    shallow = copy.copy(original)
    deep = copy.deepcopy(original)
    assert type(shallow) is Example and type(deep) is Example
    assert shallow is not original and shallow.value is original.value
    assert deep is not original and deep.value == original.value
    assert deep.value is not original.value


def list_subclass():
    class Example(list):
        pass

    original = Example([[1, 2], 3])
    original.extra = [4, 5]
    shallow = copy.copy(original)
    deep = copy.deepcopy(original)
    assert type(shallow) is Example and type(deep) is Example
    assert list(shallow) == list(deep) == list(original)
    assert shallow[0] is original[0] and shallow.extra is original.extra
    assert deep[0] is not original[0] and deep.extra is not original.extra
    assert deep.extra == original.extra


def dict_subclass():
    class Example(dict):
        pass

    original = Example(first=[1, 2])
    original.extra = [3]
    shallow = copy.copy(original)
    deep = copy.deepcopy(original)
    assert type(shallow) is Example and type(deep) is Example
    assert shallow == deep == original
    assert shallow['first'] is original['first'] and shallow.extra is original.extra
    assert deep['first'] is not original['first'] and deep.extra is not original.extra


def tuple_subclass():
    class Example(tuple):
        pass

    original = Example([[1, 2], 3])
    shallow = copy.copy(original)
    deep = copy.deepcopy(original)
    assert type(shallow) is Example and type(deep) is Example
    assert shallow == deep == original
    assert shallow is not original and shallow[0] is original[0]
    assert deep is not original and deep[0] is not original[0]


def weak_references():
    class Example:
        pass

    original = Example()
    reference = weakref.ref(original)
    assert copy.copy(reference) is reference
    assert copy.deepcopy(reference) is reference
    assert reference() is original


check('atomic', atomic)
check('lists and dicts', lists_and_dicts)
check('sets', sets)
check('frozensets', frozensets)
check('bytearrays', bytearrays)
check('instances', instances)
check('list subclass', list_subclass)
check('dict subclass', dict_subclass)
check('tuple subclass', tuple_subclass)
check('weak references', weak_references)
