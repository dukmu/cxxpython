import copyreg
import gc
import io
import pickle


class Record:
    def __init__(self, value):
        self.value = value
        self.self = self

    def __getstate__(self):
        gc.collect()
        return {'value': self.value, 'self': self.self}

    def __setstate__(self, state):
        gc.collect()
        self.__dict__.update(state)
        self.restored = True


class Slotted:
    __slots__ = ('value',)

    def __init__(self, value):
        self.value = value


class NewArgs:
    def __new__(cls, value, *, label):
        result = super().__new__(cls)
        result.value = value
        result.label = label
        return result

    def __getnewargs_ex__(self):
        gc.collect()
        return (self.value,), {'label': self.label}


def restore(value):
    gc.collect()
    return Reduced(value)


def set_state(value, state):
    gc.collect()
    value.state = state


class Reduced:
    def __init__(self, value):
        self.value = value

    def __reduce_ex__(self, protocol):
        gc.collect()
        return restore, (self.value,), {'protocol': protocol}, None, None, set_state


class Custom:
    def __init__(self, value):
        self.value = value


def reduce_custom(value):
    gc.collect()
    return Custom, (value.value + 1,)


for protocol in (1, 2, 3, 4, 5):
    record = pickle.loads(pickle.dumps(Record(42), protocol=protocol))
    assert record.value == 42 and record.self is record and record.restored
    reduced = pickle.loads(pickle.dumps(Reduced(7), protocol=protocol))
    assert reduced.value == 7 and reduced.state == {'protocol': protocol}
    print('state and reduce', protocol, True)
stream = io.BytesIO()
pickler = pickle.Pickler(stream, protocol=5)
pickler.dispatch_table = copyreg.dispatch_table.copy()
pickler.dispatch_table[Custom] = reduce_custom
pickler.dump(Custom(9))
assert pickle.loads(stream.getvalue()).value == 10
assert Custom not in copyreg.dispatch_table
print('private dispatch table', True)

for protocol in (4, 5, 2):
    slot = pickle.loads(pickle.dumps(Slotted(12), protocol=protocol))
    new = pickle.loads(pickle.dumps(NewArgs(8, label='keyword'), protocol=protocol))
    assert slot.value == 12 and (new.value, new.label) == (8, 'keyword')
    print('slots and newargs', protocol, True)

record = pickle.loads(pickle.dumps(Record(42), protocol=0))
assert record.value == 42 and record.self is record and record.restored
reduced = pickle.loads(pickle.dumps(Reduced(7), protocol=0))
assert reduced.value == 7 and reduced.state == {'protocol': 0}
print('state and reduce', 0, True)
