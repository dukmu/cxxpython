def check(f):
    try:
        f()
    except Exception as e:
        print(type(e).__name__)

class Bad:
    def __complex__(self):
        return 3

z = complex(1, 2)
check(lambda: complex('1 + 2j'))
check(lambda: complex('1__2j'))
check(lambda: complex('1', 2))
check(lambda: complex(1, '2'))
check(lambda: complex(Bad()))
check(lambda: z / 0)
check(lambda: complex() ** -1)
check(lambda: complex(1e200, 0) ** 2)
check(lambda: complex(float('inf'), 0) ** 1)
check(lambda: z < 2)
check(lambda: z // 2)
check(lambda: z % 2)
check(lambda: setattr(z, 'real', 4))
check(lambda: setattr(z, 'imag', 4))
