import itertools
import types

print(isinstance(list.append, types.MethodDescriptorType))
print(list.append.__get__(None, list) is list.append)
value = []
bound = list.append.__get__(value, list)
bound(3)
print(bound.__self__ is value, value)
for operation in [lambda: list.append({}, 1), lambda: list.append.__get__({}, dict)]:
    try:
        operation()
    except TypeError:
        print('incompatible receiver')

print(sorted(dict.fromkeys(['a', 'b'], 7).items()))
print(sorted(dict.__dict__['fromkeys'].__get__(None, dict)(['a'], 7).items()))
print(int.from_bytes(b'\x01\x02', 'big'), bytes.fromhex('61 62'))
print(list(itertools.chain.from_iterable([[1, 2], [3]])))
print(str.maketrans('a', 'b'))
print(str.__dict__['maketrans'].__get__(None, str)('a', 'b'))
print(staticmethod(list.append).__get__(None, list) is list.append)
try:
    classmethod(list.append).__get__(None, list)
except TypeError:
    print('Python classmethod preserves descriptor validation')

print([1] + [2], [1] * 2, (1, 2) * 2, 'a' * 2, b'a' * 2)
print(int.__add__(1, 2), len([1, 2, 3]), bool(1))

class Index(int):
    def __rmul__(self, other):
        return 'reflected override'

print([1] * Index(2))
