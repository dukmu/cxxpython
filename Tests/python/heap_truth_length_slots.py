class Sized:
    def __init__(self, size):
        self.size = size

    def __len__(self):
        return self.size


print(len(Sized(3)))
print(bool(Sized(0)))
print(bool(Sized(2)))
if Sized(0):
    print('bad')
else:
    print('empty')


class Truthy:
    def __init__(self, flag):
        self.flag = flag

    def __len__(self):
        return 0

    def __bool__(self):
        return self.flag


print(bool(Truthy(True)))
print(bool(Truthy(False)))


class LaterSized:
    pass


def later_len(self):
    return 4


LaterSized.__len__ = later_len
print(len(LaterSized()))
del LaterSized.__len__
try:
    len(LaterSized())
except TypeError as exc:
    print(str(exc).__contains__('no len'))


class BadLen:
    def __len__(self):
        return 'nope'


try:
    len(BadLen())
except TypeError as exc:
    print(str(exc).__contains__('__len__'))


class BadBool:
    def __bool__(self):
        return 1


try:
    bool(BadBool())
except TypeError as exc:
    print(str(exc).__contains__('__bool__'))
