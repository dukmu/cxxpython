values = [complex(), complex(3), complex(3, -4), complex(-0.0, -0.0), complex('j'), complex(' ( 1_2.5-3e1j ) '), complex('inf+nanj')]
for value in values:
    print(repr(value), str(value), value.real, value.imag, bool(value))
z = complex(3, 4)
print(z + 2, 2 + z, z - 2, 2 - z, z * 2, 2 * z)
print(z / 2, 2 / z, z ** 2, 2 ** complex(2, 0))
print(+z, -z, abs(z), z.conjugate())
print(complex(2, 0) == 2, complex(2, 0) == 2.0, complex(2, 1) != 2)
print(complex(9007199254740992, 0) == 9007199254740993)
print(hash(complex(2, 0)) == hash(2), hash(complex(2, 3)) == hash(2) + 1000003 * hash(3))
class IntSub(int):
    def __float__(self):
        return 99.0

class FloatSub(float):
    def __float__(self):
        return 99.0

print(z + IntSub(2), z + FloatSub(2))
print(complex(float('nan'), 0) == complex(float('nan'), 0))
