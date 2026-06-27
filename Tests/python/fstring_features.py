print(f'{1!r}')
print(f'{"abc"!s}')
print(f'{7:}')
print(f'{1:{2}}')
w = 3
print(f'{"x":>{w}}')
print(f'{"x":{w}}')
print(f'{True:{5}}')

def local_width():
    width = 4
    return f'{7:>{width}}'

print(local_width())
