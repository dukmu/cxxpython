# Adapted from CPython 3.12 test_collections.py: TestNamedTuple and TestCounter.
from collections import namedtuple, Counter

Point = namedtuple('Point', 'x y', defaults=[10])
p = Point(3)
print(p, p.x, p.y, p._fields, p._field_defaults)
print(p._replace(y=4), Point._make([5, 6])._asdict())
print(namedtuple('T', ['x', 'x', 'class'], rename=True)._fields)
try:
    namedtuple('T', ['x', 'x'])
except ValueError as e:
    print(type(e).__name__, str(e))
c = Counter('abracadabra')
print(c['a'], c['z'], c.total())
print(c.most_common(2))
c.update('abc')
c.subtract('aaa')
print(c['a'], c['b'], sorted((c & Counter('abc')).items()))
print(sorted((Counter(a=2, b=-1) + Counter(a=1, c=2)).items()))
