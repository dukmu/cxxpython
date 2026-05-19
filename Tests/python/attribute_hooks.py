class A:
    def __setattr__(self, name, value):
        object.__setattr__(self, 'seen_set', name)
        object.__setattr__(self, name, value)

    def __delattr__(self, name):
        object.__setattr__(self, 'seen_del', name)
        object.__delattr__(self, name)

a = A()
a.x = 'value'
print(a.seen_set)
print(a.x)
del a.x
print(a.seen_del)
