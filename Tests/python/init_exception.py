class A:
    def __init__(self, value):
        self.value = value

a = A('ready')
print(a.value)

try:
    raise TypeError('boom')
except TypeError as e:
    print(e)

try:
    raise ValueError('no')
except TypeError:
    print('wrong')
except ValueError:
    print('value')
