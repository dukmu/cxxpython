class Tuple(tuple):
    pass
for a in ((), (1, 2), Tuple((3,))):
    for b in ((), (4,), Tuple((5,))):
        print(a + b, type(a + b) is tuple)
    for count in (-2, 0, 1, 3):
        print(a * count, count * a, type(a * count) is tuple)
class Index:
    def __index__(self):
        return 2
print((1, 2) * Index())
try:
    () + []
except TypeError:
    print('concat rejects list')
