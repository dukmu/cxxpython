text = 'ababa'
for sub, start, end in (('ba', 0, None), ('', 6, None), ('', -2, -1), ('a', -100, 100), ('z', 0, None)):
    print(text.find(sub, start, end), text.rfind(sub, start, end))
print(text.index('ba'), text.rindex('ba'))
try:
    text.index('z')
except ValueError as error:
    print(str(error))
class Bound:
    def __index__(self): return 2
print(text.find('a', Bound()))

for name, method in (('startswith', text.startswith), ('endswith', text.endswith)):
    for value in (b'a', 1, ('missing', b'a'), ('a', b'a'), (), ('ba', 'ab')):
        try:
            print(name, repr(value), method(value))
        except TypeError as error:
            print(name, repr(value), type(error).__name__, str(error))
