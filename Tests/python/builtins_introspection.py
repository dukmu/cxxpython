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
