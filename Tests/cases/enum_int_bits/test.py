class Number(int):
    pass


values = (0, 1, -1, 2, -2, 7, -7, (1 << 63) - 1, -(1 << 63),
          1 << 64, (1 << 130) + (1 << 64) + 5,
          -((1 << 130) + (1 << 64) + 5), False, True, Number(-17))
for value in values:
    print(value, value.bit_length(), value.bit_count())

print(int.bit_length(256), int.bit_count(-255))
for method in (int.bit_length, int.bit_count):
    try:
        method('1')
    except TypeError:
        print('TypeError')
