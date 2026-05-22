class Thing:
    marker = 'class-marker'

    def __getattr__(self, name):
        if name == 'dynamic':
            return 'computed'
        raise AttributeError(name)


def documented():
    "documented function"
    return None


thing = Thing()
print(getattr(thing, 'marker'))
print(getattr(thing, 'dynamic'))
print(getattr(thing, 'missing', 'fallback'))
print(hasattr(thing, 'dynamic'))
print(hasattr(thing, 'missing'))
print(setattr(thing, 'added', 42))
print(thing.added)
print(dir(__builtins__).__contains__('getattr'))
print(dir(__builtins__).__contains__('setattr'))
print(dir(__builtins__).__contains__('hasattr'))
print(dir(__builtins__).__contains__('help'))
help(documented)
help()
