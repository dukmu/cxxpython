from _collections import defaultdict

calls = []
def factory():
    calls.append('called')
    return []

d = defaultdict(factory, a=[1])
d['b'].append(2)
print(d['a'], d['b'], calls, isinstance(d, dict))
print(d.get('missing'), calls)
d.default_factory = None
try:
    d['missing']
except KeyError as exc:
    print(type(exc).__name__, exc.args)
d.default_factory = lambda: 42
print(d['missing'], d['missing'])
class Derived(defaultdict):
    def __missing__(self, key):
        return 'missing:' + key

print(Derived(int)['x'])
try:
    defaultdict(1)
except TypeError as exc:
    print(type(exc).__name__, str(exc))
d = defaultdict(list)
d.default_factory = 123
print(d.default_factory)
try:
    d['absent']
except TypeError as exc:
    print(type(exc).__name__, str(exc))
print('absent' in d)
d.default_factory = list
try:
    d.__init__(int, 1)
except TypeError:
    print(d.default_factory is int)
class Subclass(defaultdict):
    __slots__ = ('extra',)

s = Subclass(list)
s.extra = 'slot'
s['item'].append(9)
print(s.extra, s['item'], s.default_factory is list, isinstance(s, dict))
