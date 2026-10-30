from contextlib import closing, nullcontext, redirect_stdout, redirect_stderr, suppress
from io import StringIO
import sys

class Resource:
    def close(self):
        print('closed')

try:
    with closing(Resource()) as obj:
        print('resource', isinstance(obj, Resource))
        raise ValueError('body')
except ValueError:
    print('body propagated')

out, err = StringIO(), StringIO()
original_out, original_err = sys.stdout, sys.stderr
redirect = redirect_stdout(out)
with redirect as target:
    print('outer', target is out)
    with redirect:
        print('inner')
with redirect_stderr(err):
    print('error', file=sys.stderr)
print('redirected', repr(out.getvalue()), repr(err.getvalue()))
print('restored', sys.stdout is original_out, sys.stderr is original_err)

with nullcontext('value') as value:
    print('null', value)
with suppress(LookupError):
    raise KeyError('ignored')
try:
    with suppress(ValueError):
        raise TypeError('not ignored')
except TypeError as exc:
    print('unsuppressed', str(exc))
