events = []
def mark(value):
    events.append(value)
    return value

class Target:
    pass
target = Target()
items = [0]
name: mark('name') = mark('value')
(parenthesized): mark('paren') = mark('paren value')
target.x: mark('attribute') = mark('attr value')
items[mark(0)]: mark('subscript') = mark('item value')
target.missing: mark('no attr rhs')
items[mark(0)]: mark('no item rhs')
bare: mark('bare')
print(events)
print(__annotations__)
print(name, parenthesized, target.x, items)

namespace = {}
exec('if False:\n    hidden: int\n', namespace)
print(namespace['__annotations__'])
namespace = {'__annotations__': {'existing': 1}}
exec('new: int', namespace)
print(namespace['__annotations__'])

class Mapping(dict):
    def __setitem__(self, key, value):
        if key == '__annotations__':
            print('class annotations setup')
        super().__setitem__(key, value)
class Meta(type):
    @classmethod
    def __prepare__(meta, name, bases):
        return Mapping()
class Example(metaclass=Meta):
    if False:
        never: int
    field: str
print(Example.__annotations__)

class BadMapping(dict):
    def __setitem__(self, key, value):
        if key == '__annotations__':
            raise ValueError('setup rejected')
        super().__setitem__(key, value)
class BadMeta(type):
    @classmethod
    def __prepare__(meta, name, bases):
        return BadMapping()
try:
    class Bad(metaclass=BadMeta):
        if False:
            x: int
except ValueError as error:
    print(type(error).__name__, str(error))

def local():
    x: missing_annotation
    try:
        return x
    except UnboundLocalError:
        return 'unbound local'
print(local(), local.__annotations__)

def local_lambda():
    x: (lambda: missing)
    return 3

def local_yield():
    x: (yield 1)

def local_await():
    x: (await missing)

print('unevaluated', local_lambda(), local_yield.__code__.co_flags,
      local_await.__code__.co_flags)
for source in ('def f():\n global x\n x: int',
               'def f():\n nonlocal x\n x: int',
               'def outer():\n x = 1\n def inner():\n  nonlocal x\n  x: int'):
    try:
        compile(source, '<test>', 'exec')
    except SyntaxError as error:
        print(type(error).__name__, error.msg)
