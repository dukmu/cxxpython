# Adapted from CPython 3.12.14 Lib/test/test_contextlib.py TestSuppress.
from contextlib import suppress


def tree(error):
    if isinstance(error, BaseExceptionGroup):
        return (type(error).__name__, error.message, tuple(tree(e) for e in error.exceptions))
    return (type(error).__name__, error.args)


# An unmatched ordinary exception must propagate, not fail on a missing builtin.
original = TypeError('ordinary')
try:
    with suppress(ValueError):
        raise original
except TypeError as caught:
    assert caught is original
    print('ordinary propagated', str(caught))

with suppress(ValueError):
    raise ExceptionGroup('all', [ValueError('one'), ValueError('two')])
print('all suppressed')
with suppress(ValueError, KeyError):
    raise ExceptionGroup('multiple', [ValueError('one'), KeyError('two')])
print('multiple suppressed')

root = ExceptionGroup('root', [ValueError('one'),
                             ExceptionGroup('inner', [KeyError('two'), ValueError('three')])])
root.add_note('preserved')
try:
    with suppress(ValueError):
        raise root
except ExceptionGroup as remaining:
    print('partial', tree(remaining))
    assert remaining.__notes__ == ['preserved']
    assert remaining.__notes__ is not root.__notes__
    trace = remaining.__traceback__
    while trace is not None and trace is not root.__traceback__:
        trace = trace.tb_next
    assert trace is root.__traceback__

with suppress(GeneratorExit):
    raise BaseExceptionGroup('base only', [GeneratorExit('stop')])
print('base suppressed')
for ignored in (KeyError, GeneratorExit):
    try:
        with suppress(ignored):
            raise BaseExceptionGroup('mixed', [GeneratorExit('stop'), KeyError('key')])
    except BaseExceptionGroup as remaining:
        print('base partial', tree(remaining))

with suppress(ExceptionGroup):
    raise root
print('whole group suppressed')
