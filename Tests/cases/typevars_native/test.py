from _typing import TypeVar, TypeVarTuple, Generic
import _weakref


def failure(label, action):
    try:
        action()
    except BaseException as error:
        print(label, type(error).__name__)
    else:
        print(label, 'no error')


class BadTruth:
    def __bool__(self):
        raise ValueError('truth')


t = TypeVar('T')
cov = TypeVar('C', covariant=True)
contra = TypeVar('D', contravariant=True)
inferred = TypeVar('I', infer_variance=True)
constraints = TypeVar('Choices', None, int)
ts = TypeVarTuple('Ts')
print('types', type(t) is TypeVar, type(ts) is TypeVarTuple, Generic.__name__)
print('defaults', t.__name__, t.__bound__, t.__constraints__,
      t.__covariant__, t.__contravariant__, t.__infer_variance__)
print('variance', repr(t), repr(cov), repr(contra), repr(inferred),
      inferred.__infer_variance__)
print('constraints', constraints.__constraints__, constraints.__constraints__[0] is None)
print('tuple', repr(ts), ts.__name__, ts.__reduce__())
print('identity', t == t, t == TypeVar('T'), hash(t) == hash(t))
print('reduce', t.__reduce__(), constraints.__reduce__())
print('weak', _weakref.ref(t)() is t, _weakref.ref(ts)() is ts)
t.extra = 42
t.__module__ = 'example'
ts.extra = 'tuple'
print('dict', t.extra, t.__module__, ts.extra)
print('parameters', list[t].__parameters__, dict[t, list[t]].__parameters__,
      list[ts].__parameters__)
failure('name', lambda: TypeVar(1))
failure('tuple name', lambda: TypeVarTuple(1))
failure('one constraint', lambda: TypeVar('X', int))
failure('both variances', lambda: TypeVar('X', covariant=True, contravariant=True))
failure('truth', lambda: TypeVar('X', covariant=BadTruth()))
failure('unknown keyword', lambda: TypeVar('X', unknown=True))
failure('readonly name', lambda: setattr(t, '__name__', 'other'))
failure('readonly constraints', lambda: setattr(t, '__constraints__', ()))
failure('readonly bound', lambda: setattr(t, '__bound__', int))
failure('typevar subclass', lambda: type('Sub', (TypeVar,), {}))
failure('tuple subclass', lambda: type('Sub', (TypeVarTuple,), {}))
failure('generic subclass type', lambda: type('Sub', (type(Generic),), {}))
