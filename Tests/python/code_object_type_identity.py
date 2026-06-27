c1 = compile('1', 'a.py', 'eval')
c2 = compile('2', 'b.py', 'eval')

def sample():
    return 1

print(c1 is c2)
print(type(c1) is type(c2))
print(type(c1) is type(sample.__code__))
print(id(type(c1)) == id(type(c2)))
print(type(c1).__name__)
