import gc
import sys

def describe(exc):
    if isinstance(exc, BaseExceptionGroup):
        return (type(exc).__name__, exc.message, [describe(child) for child in exc.exceptions])
    return (type(exc).__name__, str(exc))

class CollectingGroup(ExceptionGroup):
    def derive(self, children):
        gc.collect()
        return CollectingGroup(self.message, children)

    def __bool__(self):
        return False

leaf = ValueError('duplicate')
original = CollectingGroup('root', [leaf, ExceptionGroup('inner', [leaf, TypeError('t')])])
try:
    try:
        raise original
    except* ValueError as caught:
        print('matched false group', bool(caught), describe(caught), sys.exception() is caught)
        gc.collect()
        raise
except BaseException as result:
    print('projected', describe(result))
    print('duplicate identity', result.exceptions[0] is result.exceptions[1].exceptions[0])

class BrokenGroup(ExceptionGroup):
    def derive(self, children):
        gc.collect()
        raise LookupError('derive failed')

try:
    try:
        raise BrokenGroup('bad', [ValueError('v'), TypeError('t')])
    except* ValueError:
        print('unexpected body')
    except* TypeError:
        print('unexpected later body')
except LookupError as error:
    print('derive failure', str(error), type(error.__context__).__name__)

class NotAGroup(ExceptionGroup):
    def derive(self, children):
        return None

try:
    try:
        raise NotAGroup('bad result', [ValueError('v'), TypeError('t')])
    except* ValueError:
        pass
except TypeError as error:
    print('invalid derive', str(error))

def marker():
    print('later type active', type(sys.exception()).__name__)
    return TypeError

try:
    raise ExceptionGroup('sequential', [ValueError('v'), TypeError('t')])
except* ValueError as first:
    print('first active', sys.exception() is first)
except* marker() as second:
    print('second active', sys.exception() is second)

class Overridden(ExceptionGroup):
    def split(self, matcher):
        gc.collect()
        print('split override', matcher.__name__)
        return super().split(matcher) + ('extra entries allowed',)

try:
    raise Overridden('overridden', [ValueError('v'), TypeError('t')])
except* ValueError as part:
    print('overridden values', describe(part))
except* TypeError as part:
    print('overridden types', describe(part))

class BadSplit(ExceptionGroup):
    def split(self, matcher):
        return self.result

for answer in ('wrong', (None,)):
    error = BadSplit('bad split', [ValueError('v')])
    error.result = answer
    try:
        try:
            raise error
        except* ValueError:
            pass
    except TypeError as failure:
        print('bad split', str(failure))

original = ExceptionGroup('notes', [ValueError('v'), TypeError('t')])
original.add_note('original')
try:
    try:
        raise original
    except* ValueError as part:
        part.add_note('subgroup only')
        raise
except BaseException as result:
    print('notes reraised', describe(result), result.__notes__)

try:
    try:
        raise ValueError('naked trace')
    except* ValueError as part:
        print('naked trace', part.__traceback__.tb_next is None,
              part.__traceback__ is part.exceptions[0].__traceback__)
        print('naked args', type(part.args[1]).__name__, part.args[1] is part.exceptions)
        raise
except BaseException as result:
    print('naked reraised', describe(result))
