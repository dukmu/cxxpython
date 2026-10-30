# Adapted from CPython 3.12.14 Lib/test/test_types.py MappingProxyTests.
import types

mapping = {'a': 'A', 'b': 'B'}
view = types.MappingProxyType(mapping)
assert view == mapping
assert view['a'] == 'A'
assert view.get('missing') is None
assert view.get('missing', 42) == 42
mapping['c'] = 'C'
assert view['c'] == 'C'
try:
    view['a'] = 'changed'
except TypeError:
    pass
else:
    raise AssertionError('mapping proxy accepted assignment')
try:
    types.MappingProxyType(10)
except TypeError:
    pass
else:
    raise AssertionError('mapping proxy accepted a non-mapping')
print('types mappingproxy ok')
