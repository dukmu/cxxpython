class Intercept:
    def __getattribute__(self, name):
        if name == 'virtual':
            return 'virtual-value'
        return object.__getattribute__(self, name)


item = Intercept()
item.value = 'stored'
print(item.virtual)
print(item.value)


class Fallback:
    def __getattribute__(self, name):
        return object.__getattribute__(self, '__missing__')

    def __getattr__(self, name):
        return 'fallback:' + name


print(Fallback().field)
