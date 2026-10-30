class FromComplex:
    def __complex__(self):
        print('complex called')
        return complex(2, 3)
    def __float__(self):
        print('float called')
        return 4.0

class FromFloat:
    def __float__(self):
        return 5.5

class FromIndex:
    def __index__(self):
        return 7

class Sub(complex):
    pass

print(complex(FromComplex()), complex(FromFloat()), complex(FromIndex()))
print(complex(complex(1, 2), complex(3, 4)))
print(complex(real=2, imag=3), complex(imag=-2))
s = Sub(2, 3)
print(s, isinstance(s, Sub), type(+s) is complex, type(s.conjugate()) is complex)
for text in ['-0', '-0j', '1+j', '1-j', '1e-2+3e+2j', ' ( 3+4j ) ']:
    print(text, complex(text))
