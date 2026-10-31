def error(case_name, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except TypeError as exc:
        print(case_name, type(exc).__name__)
    else:
        raise AssertionError(case_name)


class Child(filter):
    pass


value = Child(None, [0, 1, 2])
value.label = 'child'
print('subclass', type(value) is Child, iter(value) is value, value.label, list(value))
error('subclass keyword', Child, None, [], label='bad')
error('wrong type', filter.__new__, int, None, [])
error('non-type', filter.__new__, 1, None, [])
for method in (filter.__iter__, filter.__next__, filter.__reduce__):
    for bad in (None, 1, map(bool, []), zip([])):
        error('receiver', method, bad)


class Initialized(filter):
    def __init__(self, predicate, iterable, *, label):
        self.label = label


value = Initialized(None, [0, 3], label='accepted')
print('custom init', value.label, list(value))
value = filter.__new__(Initialized, None, [0, 4], label='ignored by new')
print('direct new', type(value) is Initialized, hasattr(value, 'label'), list(value))


class Allocated(filter):
    def __new__(cls, iterable):
        return super().__new__(cls, None, iterable)


print('custom new', list(Allocated([0, 5])))
for cls in (filter, Child):
    source = iter([0, 1, 2, 3, 4])
    value = cls(bool, source)
    print('reduce first', next(value))
    rebuilt_type, args = value.__reduce__()
    print('reduce state', rebuilt_type is cls, args[0] is bool, args[1] is source)
    rebuilt = rebuilt_type(*args)
    print('reduce shared', next(rebuilt), next(value), list(rebuilt), list(value))
    rebuilt_type, args = value.__reduce__()
    print('reduce exhausted', rebuilt_type is cls, args[1] is source, list(rebuilt_type(*args)))

value = filter(None, [1])
rebuilt_type, args = value.__reduce__()
print('reduce None', rebuilt_type is filter, args[0] is None, iter(args[1]) is args[1])
error('reduce arguments', value.__reduce__, 1)
