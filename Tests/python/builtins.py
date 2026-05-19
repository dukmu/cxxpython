print(dir(__builtins__))
print(__builtins__.__name__)
print(type(__builtins__))

print(all([True, 1, 'x']))
print(all([True, 0, 'x']))
print(all([]))
print(any([False, 0, '']))
print(any([False, 0, 'yes']))

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
print(dir(__builtins__).__contains__('sum'))
