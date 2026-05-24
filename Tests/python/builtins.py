print(dir(__builtins__))
print(__builtins__.__name__)
print(type(__builtins__))
print(dir(__builtins__).__contains__('OSError'))
print(__builtins__.__doc__.__contains__('Built-in functions'))

print(all([True, 1, 'x']))
print(all([True, 0, 'x']))
print(all([]))
print(any([False, 0, '']))
print(any([False, 0, 'yes']))

def false_or_raise(value):
    if value:
        raise TypeError('all advanced too far')
    return False

def true_or_raise(value):
    if value:
        return True
    raise TypeError('any advanced too far')

print(all(map(false_or_raise, [0, 1])))
print(any(map(true_or_raise, [1, 0])))

print(sum([1, 2, 3]))
print(sum([1, 2], 10))
print(min([3, 1, 2]))
print(max([3, 1, 2]))
print(min('bee', 'ant', 'cat'))
print(max('bee', 'ant', 'cat'))
print(sorted([3, 1, 2]))
print(sorted(['bee', 'ant', 'cat']))

class Sample:
    marker = 'class'

sample = Sample()
sample.b = 2
sample.a = 1
print(vars(sample))
print(vars(sample)['a'])
print(vars()['sample'] == sample)
print(dir(__builtins__).__contains__('__name__'))
print(dir(__builtins__).__contains__('__build_class__'))
print(dir(__builtins__).__contains__('__import__'))
print(dir(__builtins__).__contains__('sum'))

print(dir(type).__contains__('__call__'))
print(dir(type).__contains__('__bases__'))
print(dir(type).__contains__('__mro__'))
print(dir(type).__contains__('mro'))
print(type.__base__)
print(type.__bases__)
print(type.__mro__)
print(type.mro(type))

print(dir(object).__contains__('__new__'))
print(dir(object).__contains__('__init__'))
print(dir(object).__contains__('__str__'))
print(dir(object).__contains__('__dir__'))
print(object.__init__(object()))
print(object.__str__(object()).__contains__('object at'))
print(dir(str).__contains__('__len__'))
print(dir(str).__contains__('__contains__'))
print(len('abc'))
print('b' in 'abc')
print('z' not in 'abc')
print('abc'.__contains__('b'))
print('abc'.__len__())
print('abc'.__getitem__(1))
print(list('abc'))
x = []
same = x
print(x is same)
print(x is [])
print(x is not [])

print(dir(int).__contains__('__bool__'))
print(dir(int).__contains__('__int__'))
print(dir(int).__contains__('__index__'))
print(dir(int).__contains__('__dict__'))
print(dir(1).__contains__('__dict__'))
print((1).__bool__())
print((1).__int__())
print((1).__index__())
print((0).__bool__())

print(range(3))
print(type(range(3)))
print(len(range(1, 8, 2)))
print(range(1, 8, 2).__getitem__(2))
print(range(5).__contains__(3))
print(4 in range(1, 8, 2))
print(list(range(3)))
print(list(range(5, 1, -2)))

print(list(enumerate(['a', 'b'])))
print(list(enumerate(['a'], 5)))
print(list(zip([1, 2], ['a', 'b', 'c'])))
print(list(zip()))

def double(value):
    return value + value

def add(left, right):
    return left + right

def at_least_two(value):
    return value >= 2

print(list(map(double, [1, 2, 3])))
print(list(map(add, [1, 2], [10, 20])))
print(list(filter(at_least_two, [1, 3, 2])))
print(list(filter(None, [0, 1, '', 'x'])))
print(dir(range).__contains__('__iter__'))
print(dir(range(3)).__contains__('__next__'))
print(dir(map).__contains__('__next__'))

print(format(123))
print(format('abc'))
code = compile('1 + 2', '<builtins-test>', 'eval')
print(eval(code))
print(eval('3 + 4'))
scope = {}
exec('answer = 42', scope)
print(scope['answer'])

def build_body():
    value = 'from-build-class'

BuiltByBuiltin = __build_class__(build_body, 'BuiltByBuiltin')
print(BuiltByBuiltin.__name__)
