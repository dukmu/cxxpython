# Adapted scenarios from CPython 3.12.14 Lib/test/test_exception_group.py.
# Ordinary except is intentional: this tests objects, not except* compilation.
import gc


def fails(label, expected, function):
    try:
        function()
    except expected as error:
        print(label, type(error).__name__)
        return error
    raise AssertionError(label)


def tree(error):
    if error is None:
        return None
    if isinstance(error, BaseExceptionGroup):
        return (type(error).__name__, error.message, tuple(tree(e) for e in error.exceptions))
    return (type(error).__name__, error.args)


assert BaseExceptionGroup.__bases__ == (BaseException,)
assert ExceptionGroup.__bases__ == (BaseExceptionGroup, Exception)
print('hierarchy', issubclass(ExceptionGroup, Exception),
      issubclass(BaseExceptionGroup, BaseException),
      issubclass(BaseExceptionGroup, Exception))

value, typed, stopped = ValueError('value'), TypeError('type'), GeneratorExit('stop')
source = [value, typed]
ordinary = BaseExceptionGroup('ordinary', source)
base = BaseExceptionGroup('base', [value, stopped])
assert type(ordinary) is ExceptionGroup
assert type(base) is BaseExceptionGroup
assert ordinary.args == ('ordinary', source)
assert ordinary.args[1] is source
assert ordinary.exceptions == (value, typed)
source.append(stopped)
assert ordinary.exceptions == (value, typed)
print('construction', str(ordinary), str(ExceptionGroup('one', [value])), str(base))
print('repr', repr(ExceptionGroup('repr', [value])))
assert ordinary.__dict__ == {}
for name in ('message', 'exceptions'):
    fails('readonly set ' + name, AttributeError, lambda: setattr(ordinary, name, None))
    fails('readonly delete ' + name, AttributeError, lambda: delattr(ordinary, name))
ordinary.__dict__['message'] = 'shadow'
assert ordinary.message == 'ordinary'
BaseExceptionGroup.__init__(ordinary, 'changed', [typed])
assert ordinary.args == ('changed', [typed])
assert ordinary.message == 'ordinary' and ordinary.exceptions == (value, typed)
print('reinit', ordinary.args, str(ordinary))


class BaseChild(BaseExceptionGroup):
    pass


class Child(ExceptionGroup):
    pass


class Mixed(BaseExceptionGroup, ValueError):
    pass


assert type(BaseChild('child', [value])) is BaseChild
assert type(BaseChild('child', [stopped])) is BaseChild
assert type(Child('child', [value])) is Child
assert type(Mixed('mixed', [typed])) is Mixed
assert type(Child('child', [value]).derive([typed])) is ExceptionGroup
fails('base child rejected', TypeError, lambda: Child('bad', [stopped]))
fails('mixed child rejected', TypeError, lambda: Mixed('bad', [stopped]))


class Sequence:
    def __getitem__(self, index):
        return (value, typed)[index]


assert ExceptionGroup('sequence', Sequence()).exceptions == (value, typed)


class BrokenSequence:
    def __getitem__(self, index):
        raise LookupError('sequence failed')


class MaskedExit(GeneratorExit):
    @property
    def __class__(self):
        return ValueError


class BrokenExit(GeneratorExit):
    @property
    def __class__(self):
        raise LookupError('class lookup failed')


assert type(BaseExceptionGroup('masked', [MaskedExit()])) is ExceptionGroup
fails('sequence callback', LookupError, lambda: ExceptionGroup('bad', BrokenSequence()))
fails('class callback', LookupError, lambda: ExceptionGroup('bad', [BrokenExit()]))
direct = BaseExceptionGroup.__new__(ExceptionGroup, 'direct', [value], ignored=True)
assert direct.message == 'direct'
for label, expected, function in (
    ('missing arguments', TypeError, lambda: ExceptionGroup()),
    ('one argument', TypeError, lambda: ExceptionGroup('bad')),
    ('extra arguments', TypeError, lambda: ExceptionGroup('bad', [value], None)),
    ('bad message', TypeError, lambda: ExceptionGroup(None, [value])),
    ('not sequence', TypeError, lambda: ExceptionGroup('bad', None)),
    ('set sequence', TypeError, lambda: ExceptionGroup('bad', {value})),
    ('dict sequence', TypeError, lambda: ExceptionGroup('bad', {0: value})),
    ('iterator sequence', TypeError, lambda: ExceptionGroup('bad', iter([value]))),
    ('empty children', ValueError, lambda: ExceptionGroup('bad', [])),
    ('non exception', ValueError, lambda: ExceptionGroup('bad', [value, 42])),
    ('exception class', ValueError, lambda: ExceptionGroup('bad', [ValueError])),
    ('base exception', TypeError, lambda: ExceptionGroup('bad', [stopped])),
    ('keywords', TypeError, lambda: ExceptionGroup('bad', [value], extra=True)),
    ('keyword only', TypeError, lambda: ExceptionGroup(message='bad', exceptions=[value])),
    ('invalid new type', TypeError, lambda: BaseExceptionGroup.__new__(int, 'bad', [value])),
    ('empty derive', ValueError, lambda: ordinary.derive([])),
):
    fails(label, expected, function)

inner = ExceptionGroup('inner', [value, typed])
root = BaseExceptionGroup('root', [inner, stopped, KeyError('key')])
matched, rest = root.split(ValueError)
print('split', tree(matched), tree(rest))
assert matched.exceptions[0].exceptions[0] is value
assert rest.exceptions[0].exceptions[0] is typed
assert rest.exceptions[1] is stopped
assert root.subgroup(BaseExceptionGroup) is root
assert root.subgroup(()) is None
assert root.split(BaseException) == (root, None)
no_match, all_rest = root.split(())
assert no_match is None and tree(all_rest) == tree(root)
print('tuple match', tree(root.subgroup((ValueError, KeyError))))
print('base promotion', type(root.subgroup(Exception)).__name__,
      type(root.subgroup(GeneratorExit)).__name__)

visited = []


def predicate(error):
    gc.collect()
    visited.append(error.message if isinstance(error, BaseExceptionGroup) else type(error).__name__)
    return error is inner


part = root.subgroup(predicate)
assert part.exceptions == (inner,)
print('predicate order', visited)
print('predicate truth', tree(inner.subgroup(lambda error: [1] if error is value else [])))


class Callable:
    def __call__(self, error):
        return True

    def method(self, error):
        return True


class TupleChild(tuple):
    pass


for index, condition in enumerate((None, 1, int, value, (ValueError, int),
                                   ((ValueError,),), TupleChild((ValueError,)),
                                   Callable(), Callable().method, bool)):
    error = fails('invalid condition ' + str(index), TypeError, lambda: root.split(condition))
    assert str(error) == 'expected a function, exception type or tuple of exception types'
    error = fails('invalid subgroup ' + str(index), TypeError, lambda: root.subgroup(condition))
    assert str(error) == 'expected a function, exception type or tuple of exception types'


def predicate_error(error):
    raise LookupError('predicate failed')


fails('predicate exception', LookupError, lambda: root.subgroup(predicate_error))


class BrokenTruth:
    def __bool__(self):
        gc.collect()
        raise LookupError('truth failed')


fails('predicate truth exception', LookupError, lambda: root.split(lambda error: BrokenTruth()))
for method in (root.split, root.subgroup, root.derive):
    fails('missing method argument', TypeError, lambda: method())
    fails('extra method argument', TypeError, lambda: method(ValueError, TypeError))
    fails('keyword method argument', TypeError, lambda: method(condition=ValueError))


class Derived(ExceptionGroup):
    def __new__(cls, message, exceptions, code):
        obj = super().__new__(cls, message, exceptions)
        obj.code = code
        return obj

    def __init__(self, message, exceptions, code):
        super().__init__(message, exceptions)

    def derive(self, exceptions):
        gc.collect()
        return Derived(self.message, exceptions, self.code)


custom = Derived('custom', [value, typed], 17)
left, right = custom.split(ValueError)
assert type(left) is Derived and type(right) is Derived
print('derive override', left.code, right.code, tree(left), tree(right))


class InvalidDerive(ExceptionGroup):
    def derive(self, exceptions):
        return 42


bad = InvalidDerive('invalid', [value, typed])
error = fails('invalid derive split', TypeError, lambda: bad.split(ValueError))
assert str(error) == 'derive must return an instance of BaseExceptionGroup'
error = fails('invalid derive subgroup', TypeError, lambda: bad.subgroup(TypeError))
assert str(error) == 'derive must return an instance of BaseExceptionGroup'


class RaisingDerive(ExceptionGroup):
    def derive(self, exceptions):
        raise LookupError('derive failed')


fails('derive exception', LookupError,
      lambda: RaisingDerive('invalid', [value, typed]).split(ValueError))


def traceback_for(error):
    try:
        raise error
    except BaseException as caught:
        return caught.__traceback__


root.__traceback__ = traceback_for(root)
inner.__traceback__ = traceback_for(inner)
root.__cause__ = LookupError('cause')
root.__context__ = RuntimeError('context')
root.add_note('root note')
inner.add_note('inner note')
matched, rest = root.split(ValueError)
for original, pieces in ((root, (matched, rest)),
                         (inner, (matched.exceptions[0], rest.exceptions[0]))):
    for piece in pieces:
        assert piece.__traceback__ is original.__traceback__
        assert piece.__cause__ is original.__cause__
        assert piece.__context__ is original.__context__
        assert piece.__notes__ == original.__notes__
        assert piece.__notes__ is not original.__notes__
    assert pieces[0].__notes__ is not pieces[1].__notes__
matched.add_note('matched only')
print('metadata', root.__notes__, matched.__notes__, rest.__notes__)
plain = ExceptionGroup('plain', [value, typed])
left, right = plain.split(ValueError)
print('split suppress context', plain.__suppress_context__,
      left.__suppress_context__, right.__suppress_context__)
for notes in (('tuple note',), 'ab', 123, {0: 'not sequence'}):
    plain.__notes__ = notes
    left, right = plain.split(ValueError)
    print('notes', getattr(left, '__notes__', None), getattr(right, '__notes__', None))


class BrokenNotes(ExceptionGroup):
    @property
    def __notes__(self):
        raise LookupError('notes lookup failed')


class BrokenNotesSetter(ExceptionGroup):
    @property
    def __notes__(self):
        return ('note',)

    @__notes__.setter
    def __notes__(self, value):
        raise LookupError('notes assignment failed')

    def derive(self, exceptions):
        return BrokenNotesSetter(self.message, exceptions)


fails('notes lookup exception', LookupError,
      lambda: BrokenNotes('notes', [value, typed]).split(ValueError))
fails('notes setter exception', LookupError,
      lambda: BrokenNotesSetter('notes', [value, typed]).subgroup(ValueError))
plain.__notes__ = BrokenSequence()
fails('notes sequence exception', LookupError, lambda: plain.split(ValueError))
fresh = root.derive([typed])
assert fresh.__traceback__ is None and fresh.__cause__ is None
assert fresh.__context__ is None and not hasattr(fresh, '__notes__')
print('derive no metadata', fresh.message, fresh.exceptions == (typed,))
alias = ExceptionGroup[ValueError]
assert alias.__origin__ is ExceptionGroup and alias.__args__ == (ValueError,)
print('generic alias', BaseExceptionGroup[Exception].__origin__ is BaseExceptionGroup)
