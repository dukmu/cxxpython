import sys
import gc

v = sys.version_info
print(type(v).__module__, type(v).__name__, isinstance(v, tuple), len(v))
print(v.major, v.minor, v.micro, v.releaselevel, v.serial)
print(v[:2] == (3, 12), tuple(v), repr(v), hash(v) == hash(tuple(v)))
print(v == (3, 12, 14, 'final', 0), v < (3, 13), (3, 11) < v)
print(v.n_fields, v.n_sequence_fields, v.n_unnamed_fields)
print(sys.hexversion == 0x030c0ef0, sys.version.startswith('3.12.14'), sys.warnoptions)
for operation in (lambda: type(v)(), lambda: type(v)(tuple(v)),
                  lambda: tuple.__new__(type(v), (1,)),
                  lambda: setattr(v, 'major', 4), lambda: setattr(type(v), 'major', 4)):
    try:
        operation()
    except (TypeError, AttributeError) as exc:
        print(type(exc).__name__)
try:
    class Version(type(v)):
        pass
except TypeError as exc:
    print(type(exc).__name__)
gc.collect()
print(v is sys.version_info, v.major == v[0])
