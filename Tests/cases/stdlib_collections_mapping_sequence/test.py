# Adapted from CPython 3.12 test_collections.py: TestChainMap and TestUserObjects;
# ordering operations also mirror test_ordered_dict.py.
from collections import OrderedDict, ChainMap, UserDict, UserList, UserString

od = OrderedDict([('a', 1), ('b', 2), ('c', 3)])
od.move_to_end('a')
print(list(od.items()), od.popitem(last=False), list(reversed(od)))
print(OrderedDict([('a', 1), ('b', 2)]) == OrderedDict([('b', 2), ('a', 1)]))
cm = ChainMap({'a': 1}, {'a': 2, 'b': 3})
print(cm['a'], cm['b'], sorted(cm), len(cm))
child = cm.new_child({'c': 4})
child['a'] = 5
print(child['a'], child.parents['a'], child['c'])
d = UserDict({'a': 1})
d.update(b=2)
print(sorted(d.items()), d.copy() == d)
u = UserList([1, 2])
u.append(3)
print(u, u[1:], u + [4])
s = UserString('abc')
print(s.upper(), s[1:], s + 'd', s * 2)
print(s.isidentifier(), s.translate({97: 65}))
