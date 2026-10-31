# Adapted from CPython 3.12.14 Lib/test/test_traceback.py StackSummary and
# exception-formatting samples. PSF license: doc/licenses/python-3.12.14.txt.
import gc
import io
import linecache
import traceback


def show(label, value):
    print(label, repr(value))


filename = '<traceback-format>'
source = (
    'def leaf():\n'
    '    raise ValueError("leaf failure")\n'
    'def middle():\n'
    '    leaf()\n'
    'def outer():\n'
    '    middle()\n'
    'def stack():\n'
    '    return traceback.extract_stack(limit=1)\n'
)
linecache.cache[filename] = (len(source), None, source.splitlines(True), filename)
namespace = {'traceback': traceback}
exec(compile(source, filename, 'exec'), namespace)
try:
    namespace['outer']()
except ValueError as caught:
    exc = caught.with_traceback(caught.__traceback__.tb_next)

tb = exc.__traceback__
summary = traceback.extract_tb(tb)
show('extracted', [tuple(frame) for frame in summary])
show('positions', [(frame.lineno, frame.end_lineno, frame.colno, frame.end_colno)
                   for frame in summary])
show('walk', [(frame.f_code.co_name, lineno) for frame, lineno in traceback.walk_tb(tb)])
for limit in (None, 0, 1, -1, 2, -2):
    show('format tb ' + str(limit), traceback.format_tb(tb, limit=limit))
show('format list', traceback.format_list(summary))
show('format exception', traceback.format_exception(exc))
show('legacy format exception', traceback.format_exception(type(exc), exc, tb))
exc.add_note('first note')
exc.add_note('second note\ncontinued')
show('notes', traceback.format_exception(exc))
snapshot = traceback.TracebackException.from_exception(exc)
gc.collect()
show('snapshot', list(snapshot.format()))
show('snapshot string', str(snapshot))
output = io.StringIO()
traceback.print_tb(tb, file=output)
assert output.getvalue() == ''.join(traceback.format_tb(tb))
output = io.StringIO()
traceback.print_exception(exc, file=output)
assert output.getvalue() == ''.join(traceback.format_exception(exc))
show('printed output matches', True)

stack = namespace['stack']()
show('extract stack', [tuple(frame) for frame in stack])
show('stack format', stack.format())
show('none traceback', traceback.extract_tb(None))

old_style = [('foo.py', 1, 'fred', 'line')]
show('old style summary', traceback.StackSummary.from_list(old_style).format())
frames = [traceback.FrameSummary('foo.py', 1, 'fred', line='line')] * 7
show('recursive cutoff', traceback.StackSummary.from_list(frames).format())


class Local:
    def __repr__(self):
        gc.collect()
        return '<local>'


frame = traceback.FrameSummary('locals.py', 2, 'sample', line='  work()\n',
                               locals={'z': Local(), 'a': 42})
show('summary locals', traceback.StackSummary.from_list([frame]).format())

# Exercise actual cause/context paths with stable source names, not test paths.
chain_source = (
    'def chained(mode):\n'
    '    try:\n'
    '        raise ValueError("inside")\n'
    '    except ValueError as inner:\n'
    '        if mode == "cause":\n'
    '            raise RuntimeError("outside") from inner\n'
    '        if mode == "suppressed":\n'
    '            raise RuntimeError("outside") from None\n'
    '        raise RuntimeError("outside")\n'
)
chain_file = '<traceback-chain>'
linecache.cache[chain_file] = (len(chain_source), None, chain_source.splitlines(True), chain_file)
exec(compile(chain_source, chain_file, 'exec'), namespace)
for mode in ('cause', 'context', 'suppressed'):
    try:
        namespace['chained'](mode)
    except RuntimeError as caught:
        caught.__traceback__ = caught.__traceback__.tb_next
        show('chain ' + mode, traceback.format_exception(caught))
        show('no chain ' + mode, traceback.format_exception(caught, chain=False))

group_source = (
    'def grouped():\n'
    '    raise ExceptionGroup("eg", [ValueError(1), TypeError(2)])\n'
)
group_file = '<traceback-group>'
linecache.cache[group_file] = (len(group_source), None, group_source.splitlines(True), group_file)
exec(compile(group_source, group_file, 'exec'), namespace)
try:
    namespace['grouped']()
except ExceptionGroup as caught:
    caught.__traceback__ = caught.__traceback__.tb_next
    show('raised group', traceback.format_exception(caught))
