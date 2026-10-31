from _typing import ParamSpec, ParamSpecArgs, ParamSpecKwargs, TypeVar
import _weakref
import gc


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


p = ParamSpec('P')
q = ParamSpec('Q', covariant=True)
args = p.args
kwargs = p.kwargs
print('defaults', p.__name__, p.__bound__, p.__covariant__,
      p.__contravariant__, p.__infer_variance__)
print('repr', repr(p), repr(q), repr(args), repr(kwargs))
print('types', type(args) is ParamSpecArgs, type(kwargs) is ParamSpecKwargs)
print('origin', args.__origin__ is p, kwargs.__origin__ is p)
print('equality', args == p.args, kwargs == p.kwargs, args == kwargs,
      args == ParamSpec('P').args, p == ParamSpec('P'))
print('reduce', p.__reduce__())
print('weak', _weakref.ref(p)() is p)
print('view weak', _weakref.ref(args)() is args, _weakref.ref(kwargs)() is kwargs)
print('constructed', repr(ParamSpecArgs(origin='origin')),
      repr(ParamSpecKwargs(origin=None)), ParamSpecArgs(None) == ParamSpecArgs(None))

class Origin:
    def __repr__(self):
        gc.collect()
        return 'origin'

    def __eq__(self, other):
        gc.collect()
        return 17

print('callbacks', repr(ParamSpecArgs(Origin())), ParamSpecArgs(Origin()) == ParamSpecArgs(Origin()))
p.extra = 7
p.__module__ = 'example'
print('dict', p.extra, p.__module__)
t = TypeVar('T')
print('parameters', list[p].__parameters__, dict[t, list[p]].__parameters__)
failure('name', lambda: ParamSpec(1))
failure('truth', lambda: ParamSpec('X', contravariant=BadTruth()))
failure('unknown keyword', lambda: ParamSpec('X', unknown=True))
failure('readonly name', lambda: setattr(p, '__name__', 'X'))
failure('readonly bound', lambda: setattr(p, '__bound__', int))
failure('readonly args', lambda: setattr(p, 'args', 1))
failure('readonly origin', lambda: setattr(args, '__origin__', q))
failure('hash args', lambda: hash(args))
failure('hash kwargs', lambda: hash(kwargs))
failure('paramspec subclass', lambda: type('Sub', (ParamSpec,), {}))
failure('args subclass', lambda: type('Sub', (ParamSpecArgs,), {}))
failure('kwargs subclass', lambda: type('Sub', (ParamSpecKwargs,), {}))
