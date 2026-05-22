class A:
    marker = 'a'

a = A()
print(isinstance(a, A))
print(isinstance(a, object))
print(isinstance(a, (int, A)))
print(issubclass(A, object))
print(issubclass(bool, int))
print(callable(A), callable(print), callable(a))
print(id(a) == id(a))
flag = 'visible'
print(globals()['flag'])
print(locals()['flag'])
print('a', 'b', sep=':', end='!')
print('done')
print(print.__name__)
print(print.__doc__.__contains__('Print objects'))
print(print.__text_signature__)
print(len.__text_signature__)
print(sum.__text_signature__)
print(format.__text_signature__)
print(compile.__doc__.__contains__('Compile source'))
print(eval.__text_signature__)
print(exec.__text_signature__)
print(__import__.__text_signature__)
print(__build_class__.__doc__.__contains__('class statements'))
print(list.append.__name__)
print(list.append.__doc__.__contains__('Append object'))
print(list.append.__text_signature__)

def sample_signature(alpha, beta, *items, **options):
    "sample docs"
    return alpha

print(sample_signature.__name__)
print(sample_signature.__doc__)
print(sample_signature.__text_signature__)
