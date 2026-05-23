print(str())
print(str('text'))
print(str(42))
print(str([1, 'x']))
print('-'.join(['a', 'b', 'c']))
print(''.join([]))
print('abc'.startswith('a'))
print('abc'.startswith('z'))
print('abc'.endswith('c'))
print('abc'.endswith('z'))
try:
    ','.join([1])
except TypeError as exc:
    print(str(exc).__contains__('sequence item is not str'))
