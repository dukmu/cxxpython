def answer():
    return 'ok'

def identity(value):
    return value

def implicit():
    value = 'ignored'

print(answer())
print(identity('arg'))
print(implicit())
