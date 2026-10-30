print(list(enumerate(['a', 'b'], 999999999999999999999999)))
print(list(enumerate('xy', start=-2)))
print(list(zip([1, 2], 'abc')))
print(list(zip()))
print(list(zip([1, 2], [3, 4], strict=True)))
try:
    print(list(zip([1], [2, 3], strict=True)))
except ValueError as error:
    print(str(error))
try:
    print(list(zip([1, 2], [3], strict=True)))
except ValueError as error:
    print(str(error))
def add(a, b):
    return a + b
print(list(map(add, [1, 2], [10, 20, 30])))
print([list(x) for x in map(iter, [[1, 2], [3]])])
try:
    map(add)
except TypeError:
    print('map needs iterable')
