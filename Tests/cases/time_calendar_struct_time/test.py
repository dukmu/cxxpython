import gc
import time
import weakref


def error(label, function, *args):
    try:
        function(*args)
    except (TypeError, AttributeError, IndexError, RuntimeError) as exc:
        print(label, type(exc).__name__, str(exc))
    else:
        raise AssertionError(label)


# Tuple, repr and metadata contracts follow pinned test_structseq.py.
base = (2000, 1, 2, 3, 4, 5, 6, 2, -1)
records = [time.struct_time(base),
           time.struct_time(base + ('CUSTOM',)),
           time.struct_time(base + ('CUSTOM', 1234)),
           time.struct_time(base, {'tm_zone': 'DICT', 'tm_gmtoff': -3600}),
           time.struct_time(base + ('SEQUENCE',),
                            {'tm_zone': 'ignored', 'tm_gmtoff': 1800}),
           time.struct_time(base + ('BOTH', 42),
                            {'tm_zone': 'ignored', 'tm_gmtoff': 99})]
for record in records:
    assert type(record) is time.struct_time and isinstance(record, tuple)
    assert len(record) == 9 and tuple(record) == base
    assert record == base and hash(record) == hash(base)
    assert record[:3] == base[:3] and record[::-1] == base[::-1]
    assert record[-1] == -1 and record + (7,) == base + (7,)
    constructor, args = record.__reduce__()
    rebuilt = constructor(*args)
    assert type(rebuilt) is time.struct_time and rebuilt == record
    assert rebuilt.tm_zone == record.tm_zone
    assert rebuilt.tm_gmtoff == record.tm_gmtoff
    print('tails', record.tm_zone, record.tm_gmtoff, args[1])
print('repr', repr(records[0]))
print('metadata', time.struct_time.__module__, time.struct_time.__name__,
      time.struct_time.n_fields, time.struct_time.n_sequence_fields,
      time.struct_time.n_unnamed_fields, time._STRUCT_TM_ITEMS)
print('match', time.struct_time.__match_args__)
print('characters', ''.join(time.struct_time('123456789')))

class Mapping(dict):
    def __getitem__(self, key):
        raise AssertionError('dict subclass lookup must not call __getitem__')

record = time.struct_time(sequence=base, dict=Mapping(tm_zone='SUB', tm_gmtoff=60))
print('dict subclass', record.tm_zone, record.tm_gmtoff)
for count in (8, 12):
    error('length ' + str(count), time.struct_time, (0,) * count)
error('dict type', time.struct_time, base, None)
error('sequence type', time.struct_time, None)
error('index', record.__getitem__, 9)
for field in ('tm_year', 'tm_zone', 'tm_gmtoff', 'extra'):
    error('set ' + field, setattr, record, field, 1)
    error('del ' + field, delattr, record, field)
time.struct_time.fixture_marker = 'mutable type namespace'
print('type attribute', time.struct_time.fixture_marker)
del time.struct_time.fixture_marker
try:
    class SubTime(time.struct_time):
        pass
except TypeError as exc:
    print('subclass', type(exc).__name__, str(exc))
else:
    raise AssertionError('struct_time must not be subclassable')

class Payload:
    def __bool__(self):
        gc.collect()
        return True

payload = Payload()
reference = weakref.ref(payload)
record = time.struct_time(base, {'tm_zone': payload, 'tm_gmtoff': payload})
payload.record = record
del payload
gc.collect()
assert record.tm_zone is reference() and bool(record.tm_gmtoff)
constructor, args = record.__reduce__()
assert args[1]['tm_zone'] is reference()
del record, args
gc.collect()
print('tail cycle collected', reference() is None)

class Sequence:
    def __iter__(self):
        for item in base + ('ITER', 120):
            gc.collect()
            yield item

record = time.struct_time(Sequence())
print('collecting sequence', tuple(record), record.tm_zone, record.tm_gmtoff)
