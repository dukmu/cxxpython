x = [1, 2]
print(x.__len__())
print(list.__len__(x))
print(x.__getitem__(0))
x.__setitem__(1, 5)
print(x)
x.__delitem__(0)
print(x)
print(x.__contains__(5))

it = x.__iter__()
print(it.__next__())

d = {'a': 1}
print(d.__len__())
print(d.__getitem__('a'))
d.__setitem__('b', 2)
print(d)
d.__delitem__('a')
print(d)
print(d.__contains__('b'))
dit = d.__iter__()
print(dit.__next__())

a = 1
print(a.__add__(2))
print(a.__lt__(2))
s = 'a'
print(s.__add__('b'))
print(s.__lt__('b'))
print(repr([1]))
print(hash('abc') == 'abc'.__hash__())
