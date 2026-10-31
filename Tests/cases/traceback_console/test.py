# Actual exceptions compiled with the REPL's <console> filename; no cached source.
# Reference behavior: CPython 3.12.14 Lib/traceback.py (PSF license).
import gc
import io
import linecache
import traceback


def show(label, value):
    print(label, repr(value))


source = (
    'def leaf():\n'
    '    raise ValueError("console failure")\n'
    'def outer():\n'
    '    leaf()\n'
    'def chained():\n'
    '    try:\n'
    '        leaf()\n'
    '    except ValueError as inner:\n'
    '        raise RuntimeError("console wrapper") from inner\n'
    'def grouped():\n'
    '    raise ExceptionGroup("console group", [ValueError("v"), TypeError("t")])\n'
)
linecache.cache.pop('<console>', None)
namespace = {}
exec(compile(source, '<console>', 'exec'), namespace)
for name in ('outer', 'chained', 'grouped'):
    try:
        namespace[name]()
    except Exception as caught:
        caught.__traceback__ = caught.__traceback__.tb_next
        caught.add_note('console note\ncontinued')
        show(name + ' format', traceback.format_exception(caught))
        show(name + ' extract', [tuple(frame) for frame in
                                 traceback.extract_tb(caught.__traceback__)])
        show(name + ' format tb', traceback.format_tb(caught.__traceback__))
        show(name + ' no chain', traceback.format_exception(caught, chain=False))
        snapshot = traceback.TracebackException.from_exception(caught, lookup_lines=False)
        assert all(frame._line is None for frame in snapshot.stack)
        gc.collect()
        show(name + ' deferred', list(snapshot.format()))
        output = io.StringIO()
        traceback.print_exception(caught, file=output)
        assert output.getvalue() == ''.join(traceback.format_exception(caught))

syntax = SyntaxError('invalid syntax', ('<console>', 2, 5, '    broken +\n', 2, 11))
syntax.add_note('console syntax note')
show('syntax', traceback.format_exception(syntax))
show('no cached source', '<console>' not in linecache.cache)
