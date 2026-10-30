# Behavioral cases adapted from CPython 3.12.14 Lib/test/test_copy.py (PSF license).
import copy
import gc
import weakref


def check(name, action):
    try:
        action()
    except Exception as exc:
        print(name, type(exc).__name__, str(exc))
    else:
        print(name, 'ok')


def aliases():
    shared = [42]
    original = [shared, shared, {'shared': shared}]
    shallow = copy.copy(original)
    deep = copy.deepcopy(original)
    assert shallow is not original and shallow[0] is shared
    assert deep[0] is not shared
    assert deep[0] is deep[1] and deep[0] is deep[2]['shared']


def recursive_containers():
    original = []
    original.append(original)
    result = copy.deepcopy(original)
    assert result is not original and result[0] is result
    original = {}
    original['self'] = original
    result = copy.deepcopy(original)
    assert result is not original and result['self'] is result
    original = ([],)
    original[0].append(original)
    result = copy.deepcopy(original)
    assert result is not original and result[0] is not original[0]
    assert result[0][0] is result


def memo():
    original = []
    memo = {}
    result = copy.deepcopy(original, memo)
    assert memo[id(original)] is result
    assert memo[id(memo)][0] is original
    assert copy.deepcopy(original, memo) is result
    marker = object()
    assert copy.deepcopy(original, {id(original): marker}) is marker
    original = [(1, 2)]
    memo = {}
    result = copy.deepcopy(original, memo)
    assert result == original and len(memo) == 2


def recursive_instance():
    class Example:
        pass

    original = Example()
    original.self = original
    original.items = [original]
    result = copy.deepcopy(original)
    assert result is not original
    assert result.self is result and result.items[0] is result


def custom_memo():
    class Example:
        def __deepcopy__(self, memo):
            result = Example()
            memo[id(self)] = result
            result.link = copy.deepcopy(self.link, memo)
            return result

    original = Example()
    original.link = original
    result = copy.deepcopy(original)
    assert result is not original and result.link is result


def bound_method():
    class Example:
        def method(self):
            return self

    original = Example()
    original.callback = original.method
    result = copy.deepcopy(original)
    assert result is not original
    assert result.callback.__self__ is result
    assert result.callback.__func__ is original.callback.__func__
    assert result.callback == result.method and result.callback() is result


class Item:
    def __init__(self, number):
        self.number = number


def weak_keys():
    first, second = Item(1), Item(2)
    original = weakref.WeakKeyDictionary({first: second})
    shallow = copy.copy(original)
    deep = copy.deepcopy(original)
    assert shallow is not original and shallow[first] is second
    assert deep is not original and deep[first] is not second
    assert deep[first].number == second.number
    del first
    gc.collect()
    assert len(original) == len(shallow) == len(deep) == 0


def weak_values():
    first, second = Item(1), Item(2)
    original = weakref.WeakValueDictionary({first: second})
    shallow = copy.copy(original)
    deep = copy.deepcopy(original)
    assert shallow is not original and shallow[first] is second
    keys = list(deep)
    assert len(keys) == 1 and keys[0] is not first
    assert keys[0].number == first.number and deep[keys[0]] is second
    del second
    gc.collect()
    assert len(original) == len(shallow) == len(deep) == 0


def dead_weakref():
    original = Item(3)
    reference = weakref.ref(original)
    del original
    gc.collect()
    assert reference() is None
    assert copy.copy(reference) is reference
    assert copy.deepcopy(reference) is reference


check('aliases', aliases)
check('recursive containers', recursive_containers)
check('memo', memo)
check('recursive instance', recursive_instance)
check('custom memo', custom_memo)
check('bound method', bound_method)
check('weak keys', weak_keys)
check('weak values', weak_values)
check('dead weakref', dead_weakref)
