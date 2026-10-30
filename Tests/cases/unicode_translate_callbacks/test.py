# CPython 3.12 charmap translation: Lookups, ASCII cache, and callback fallback.
import gc

class Mapping:
    def __init__(self):
        self.calls = []
    def __getitem__(self, key):
        gc.collect()
        self.calls.append(key)
        if key == 97:
            return 65
        if key == 98:
            return None
        raise LookupError('missing')

m = Mapping()
print('aabbcc'.translate(m), m.calls)
m = Mapping()
print('\u03b1aa'.translate(m), m.calls)

class Changing:
    def __init__(self):
        self.calls = 0
    def __getitem__(self, key):
        self.calls += 1
        if self.calls == 1:
            return 'long'
        return 'X'

m = Changing()
print('aa'.translate(m), m.calls)
class Broken:
    def __getitem__(self, key):
        raise RuntimeError('callback failed')
try:
    'a'.translate(Broken())
except RuntimeError as error:
    print(type(error).__name__, str(error))
print(''.translate(Broken()))
