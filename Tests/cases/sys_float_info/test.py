import gc
import sys

names = ('max', 'max_exp', 'max_10_exp', 'min', 'min_exp', 'min_10_exp',
         'dig', 'mant_dig', 'epsilon', 'radix', 'rounds')
info = sys.float_info
print(info)
print(tuple(info))
print([getattr(info, name) == info[index] for index, name in enumerate(names)])
print(len(info), info.n_fields, info.n_sequence_fields, info.n_unnamed_fields)
print(hash(info) == hash(tuple(info)))
kind = type(info)
print(kind(range(11)))
print(kind(sequence=range(11), dict={}))
for action in (lambda: kind(()), lambda: kind(range(12)),
               lambda: kind(range(11), None), lambda: setattr(info, 'max', 1),
               lambda: setattr(kind, 'max', 1)):
    try:
        action()
    except (TypeError, AttributeError) as error:
        print(type(error).__name__)
    else:
        raise AssertionError('expected immutable record or constructor error')
try:
    class Child(kind):
        pass
except TypeError:
    print('subclass rejected')
else:
    raise AssertionError('expected TypeError')


class Sequence:
    def __iter__(self):
        for value in range(11):
            gc.collect()
            yield value


print(kind(Sequence()))
gc.collect()
print(info.max_10_exp, info.mant_dig)
