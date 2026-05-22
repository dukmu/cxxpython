add = lambda a, b=3: a + b
print(add(2))
print(add(2, 4))
print(add.__name__)
print(add.__qualname__)
print(add.__doc__)
print(add.__text_signature__)

def outer(base):
    return lambda value, *, scale=2: base + value * scale

f = outer(10)
print(f(3))
print(f(3, scale=4))
print(f.__text_signature__)
print((lambda x: x if x else 'zero')(0))
print((lambda *items, **kw: len(items) + len(kw))(1, 2, a=3))

def nested(start):
    return (lambda step: (lambda: start + step))

print(nested(4)(5)())
