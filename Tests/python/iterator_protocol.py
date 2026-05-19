values = [1, 'x', False]
it = iter(values)
print(next(it))
print(next(it))
print(list(values))
print(list((1, 'tuple')))
print(list({'a': 1, 'b': 2}))
