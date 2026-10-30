import types
import gc

class Mapping:
    def __init__(self):
        self.data = {'a': 1, 'b': 2}
    def __getitem__(self, key):
        gc.collect()
        return self.data[key]
    def __iter__(self):
        return iter(self.data)
    def __len__(self):
        return len(self.data)
    def __repr__(self):
        return 'Mapping()'
    def get(self, key, default=None):
        return self.data.get(key, default)
    def keys(self):
        return self.data.keys()
    def values(self):
        return self.data.values()
    def items(self):
        return self.data.items()
    def copy(self):
        return self.data.copy()

source = Mapping()
p = types.MappingProxyType(source)
print(repr(p), len(p), list(p), p['a'])
print(p.get('missing'), p.get('missing', 7), 'a' in p, 'x' in p)
print(list(p.keys()), list(p.values()), list(p.items()), p.copy())
source.data['c'] = 3
print(p['c'], len(p), p == types.MappingProxyType(source))
class Missing(dict):
    def __missing__(self, key):
        return 'missing:' + key
print(types.MappingProxyType(Missing())['other'])
for value in ([], (), 10):
    try:
        types.MappingProxyType(value)
    except TypeError:
        print('TypeError')
print(types.MappingProxyType('abc')[1])
