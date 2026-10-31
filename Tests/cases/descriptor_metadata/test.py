import gc
import inspect


class Plain:
    pass


class Slotted:
    __slots__ = ('value', '__weakref__')


for owner, name in ((Plain, '__dict__'), (Plain, '__weakref__'), (Slotted, 'value'),
                    (type(lambda: None), '__code__'), (type(lambda: None), '__globals__')):
    descriptor = owner.__dict__[name]
    print(descriptor.__name__, descriptor.__objclass__ is owner, descriptor.__qualname__)
    print(descriptor.__name__ is descriptor.__name__)
    assert descriptor.__qualname__ is descriptor.__qualname__
    gc.collect()
    assert descriptor.__objclass__ is owner
    try:
        descriptor.__name__ = 'changed'
    except AttributeError:
        print('readonly name')
    else:
        raise AssertionError('expected readonly descriptor metadata')

print(inspect.getattr_static(Plain, '__dict__') is Plain.__dict__['__dict__'])
print(inspect.getattr_static(Slotted, 'value') is Slotted.__dict__['value'])
descriptor = Plain.__dict__['__dict__']
Plain.__qualname__ = 'Renamed'
print(descriptor.__qualname__)

class Delayed:
    __slots__ = ('value',)

descriptor = Delayed.__dict__['value']
Delayed.__qualname__ = 'BeforeFirstRead'
print(descriptor.__qualname__)
Delayed.__qualname__ = 'AfterFirstRead'
gc.collect()
print(descriptor.__qualname__)

class Meta(type):
    answer = 42
    def __getattribute__(cls, name):
        if name == '__qualname__':
            gc.collect()
            return Meta.answer
        return super().__getattribute__(name)

class Checked(metaclass=Meta):
    __slots__ = ('value',)

descriptor = Checked.__dict__['value']
try:
    descriptor.__qualname__
except TypeError as error:
    print(str(error))
else:
    raise AssertionError('expected string owner qualname')
Meta.answer = 'Valid'
print(descriptor.__qualname__)
Meta.answer = 'Cached'
print(descriptor.__qualname__)

for namespace in ({}, {'__name__': None}, {'__name__': 42}, {'__name__': 'other'}):
    original = {}
    namespace['original'] = original
    exec('result = type("Dynamic", (), original)', namespace)
    print(repr(getattr(namespace['result'], '__module__', 'missing')), original)

original = {'__module__': 'explicit'}
result = type('Explicit', (), original)
print(result.__module__, original)

from types import FunctionType, GetSetDescriptorType, MemberDescriptorType
print('descriptor types', GetSetDescriptorType is not MemberDescriptorType,
      isinstance(Slotted.value, MemberDescriptorType))
function = lambda: None
for name in ('__globals__', '__builtins__', '__closure__'):
    descriptor = getattr(FunctionType, name)
    assert isinstance(descriptor, MemberDescriptorType)
    assert descriptor.__get__(function) is getattr(function, name)
    for change in (lambda: descriptor.__set__(function, None),
                   lambda: descriptor.__delete__(function)):
        try:
            change()
        except AttributeError as error:
            print(name, str(error))
        else:
            raise AssertionError('expected readonly native member')

import _thread

class Concurrent:
    __slots__ = ('value',)

descriptor = Concurrent.__dict__['value']
names = []
errors = []
done = [_thread.allocate_lock() for _ in range(4)]
for lock in done:
    lock.acquire()

def read_names(index):
    try:
        for _ in range(20):
            names.append(descriptor.__qualname__)
            gc.collect()
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        done[index].release()

for index in range(4):
    _thread.start_new_thread(read_names, (index,))
for lock in done:
    lock.acquire()
assert not errors, errors
assert len(names) == 80
print('shared qualname', all(name is names[0] for name in names))
