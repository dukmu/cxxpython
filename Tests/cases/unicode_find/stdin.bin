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
