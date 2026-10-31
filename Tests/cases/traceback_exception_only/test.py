# Samples adapted from CPython 3.12.14 Lib/test/test_traceback.py.
# PSF license: doc/licenses/python-3.12.14.txt.
import gc
import traceback


def show(label, value):
    print(label, repr(value))


for exc in (ValueError('bad value'), KeyError('key'), KeyboardInterrupt(),
            SystemExit(3), SyntaxError('plain'), None):
    show('exception only', traceback.format_exception_only(exc))
show('legacy exception only', traceback.format_exception_only(ValueError, ValueError('legacy')))

exc = ValueError('noted')
exc.add_note('a note')
exc.add_note('multi\nline\n')
show('notes', traceback.format_exception_only(exc))
exc.__notes__ = 'not a sequence of notes'
show('string notes', traceback.format_exception_only(exc))


class Broken(Exception):
    def __str__(self):
        gc.collect()
        raise ValueError('cannot stringify')


show('bad str', traceback.format_exception_only(Broken()))

for details in (
    ('example.py', 3, 5, '    broken +\n'),
    ('example.py', 3, 5, '    broken +\n', 3, 11),
    ('example.py', 3, None, '    broken +\n'),
    ('example.py', None, None, None),
    ('', 0, 5, 'hello'),
    ('tabs.py', 4, 3, '\t broken\n', 4, 6),
    ('unicode.py', 7, 3, '  \u4e2d\u6587 +\n', 7, 5),
):
    exc = SyntaxError('invalid syntax', details)
    exc.add_note('syntax note')
    show('syntax', traceback.format_exception_only(exc))

inner = ValueError('root')
inner.add_note('inner note')
group = ExceptionGroup('nested', [inner, ExceptionGroup('child', [TypeError('t'), OSError('o')])])
group.add_note('group note\ncontinued')
show('group only', traceback.format_exception_only(group))
show('nested group', traceback.format_exception(group))
wide = ExceptionGroup('wide', [ValueError(str(i)) for i in range(5)])
show('group width', list(traceback.TracebackException.from_exception(wide,
                            max_group_width=2).format()))
show('group depth', list(traceback.TracebackException.from_exception(group,
                            max_group_depth=1).format()))

cause = ValueError('cause')
outer = RuntimeError('outer')
outer.__cause__ = cause
cause.__cause__ = outer
show('cyclic cause', traceback.format_exception(outer))

for function, args in (
    (traceback.format_exception, (42,)),
    (traceback.format_exception, (ValueError, ValueError('partial'))),
):
    try:
        function(*args)
    except (TypeError, ValueError) as exc:
        show('argument error', (type(exc).__name__, str(exc)))
    else:
        raise AssertionError('expected an argument error')
