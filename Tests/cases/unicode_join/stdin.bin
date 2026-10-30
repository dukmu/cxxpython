print(','.join(['a', 'b']), ''.join('abc'), '-'.join(()))
class S(str): pass
print(type(''.join([S('only')])).__name__)
events = []
def items():
    events.append(1)
    yield 'a'
    events.append(2)
    yield 3
    events.append(3)
    yield 'b'
try:
    ','.join(items())
except TypeError as error:
    print(str(error), events)
