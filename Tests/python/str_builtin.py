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
print('  alpha \n'.strip())
print('xxalphaxy'.strip('xy'))
print('xxalpha'.lstrip('x'))
print('alphayy'.rstrip('y'))
print('  alpha  '.strip(None))
print('alpha'.strip(''))
try:
    ','.join([1])
except TypeError as exc:
    print(str(exc).__contains__('sequence item is not str'))
try:
    'alpha'.strip(1)
except TypeError as exc:
    print(str(exc).__contains__('str.strip() argument must be str or None'))
