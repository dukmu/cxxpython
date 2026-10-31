import ast
import functools
import gc
import inspect
import linecache
import sys

assert type(sys.getrecursionlimit()) is int and sys.getrecursionlimit() > 0


def func(a: int, /, b=2, *values, c: str='value', **keywords) -> tuple:
    '''Summary.

        Body.
    '''
    local = (a, b, values, c, keywords)
    return local


signature = inspect.signature(func)
print(signature)
print(inspect.getfullargspec(func))
print([(name, str(parameter.kind)) for name, parameter in signature.parameters.items()])
print(signature.bind(1, 3, 4, c='x', extra=5).arguments)
bound = signature.bind_partial(1)
bound.apply_defaults()
print(bound.arguments)
print(bound.args, bound.kwargs)
print(signature.return_annotation is tuple)
code = func.__code__
print(code.co_argcount, code.co_posonlyargcount, code.co_kwonlyargcount, code.co_nlocals)
print(code.co_varnames)
print(inspect.getdoc(func))
print(inspect.cleandoc('First\n\tsecond\n\tthird\n'))
print(inspect.get_annotations(func))


class Sample:
    def method(self, value=3):
        return value

    @classmethod
    def create(cls, value):
        return cls()


print(inspect.signature(Sample.method))
print(inspect.signature(Sample().method))
print(inspect.signature(Sample.create))
print(inspect.ismethod(Sample().method), inspect.isfunction(Sample.method), inspect.isclass(Sample))
print(inspect.signature(functools.partial(func, 1, c='fixed')))


@functools.wraps(func)
def wrapped(*args, **kwargs):
    return func(*args, **kwargs)


print(inspect.unwrap(wrapped) is func)
print(inspect.signature(wrapped) == inspect.signature(func))


def generator():
    yield 1


async def coroutine():
    return 1


async def asynchronous_generator():
    yield 1


print(inspect.isgeneratorfunction(generator), inspect.iscoroutinefunction(coroutine),
      inspect.isasyncgenfunction(asynchronous_generator))
source = 'def cached(value):\n    return value + 1\n'
filename = '<inspect-source>'
linecache.cache[filename] = (len(source), None, source.splitlines(True), filename)
space = {}
exec(compile(source, filename, 'exec'), space)
print(inspect.getsource(space['cached']) == source)
print(inspect.getsourcelines(space['cached'])[1])
print(ast.get_docstring(ast.parse('"""Summary\n    Detail"""')))
gc.collect()
print(str(inspect.signature(func)))
