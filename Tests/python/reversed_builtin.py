print(list(reversed([1, 2, 3])))
print(list(reversed('abc')))
print(list(reversed((1, 2))))
iterator = reversed([1])
print(iterator.__iter__() is iterator)
print(iterator.__next__())
try:
    iterator.__next__()
except StopIteration as exc:
    print(type(exc))

class Custom:
    def __reversed__(self):
        return iter(['custom'])

print(list(reversed(Custom())))
