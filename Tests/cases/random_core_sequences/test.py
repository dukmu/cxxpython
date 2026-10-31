import _random

for seed in [0, 1, -1, 123456789012345678901234567890, -(1 << 500) + 123]:
    r = _random.Random(seed)
    print([r.getrandbits(k) for k in (0, 1, 5, 32, 33, 64, 100, 521)])
    print([r.random() for i in range(3)])

class Integer(int):
    def __abs__(self):
        raise RuntimeError('must not call abs override')

a = _random.Random(Integer(-123))
b = _random.Random(123)
print(a.getrandbits(200) == b.getrandbits(200))

class Hashed:
    def __hash__(self):
        return -17

print(_random.Random(Hashed()).getrandbits(96) == _random.Random((1 << 64) - 17).getrandbits(96))
print(_random.Random().getstate() != _random.Random().getstate())
