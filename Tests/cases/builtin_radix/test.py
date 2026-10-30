import gc

for value in (0, 1, -1, 17, -17, 2**130 + 3, -(2**130 + 3), True):
    print(bin(value), oct(value), hex(value))
class Index:
    def __index__(self):
        gc.collect()
        return 255
print(bin(Index()), oct(Index()), hex(Index()))
class IntOnly:
    def __int__(self):
        return 3
try:
    bin(IntOnly())
except TypeError:
    print('needs index')
