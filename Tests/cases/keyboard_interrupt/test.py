# CPython 3.12.14 Objects/exceptions.c defines KeyboardInterrupt as a simple
# BaseException subclass. PSF license: doc/licenses/python-3.12.14.txt.
import builtins
import gc


def show(label, value):
    print(label, repr(value))


show('canonical', builtins.KeyboardInterrupt is KeyboardInterrupt)
show('module', KeyboardInterrupt.__module__)
show('name', KeyboardInterrupt.__name__)
show('doc', KeyboardInterrupt.__doc__)
show('bases', [base.__name__ for base in KeyboardInterrupt.__bases__])
show('mro', [base.__name__ for base in KeyboardInterrupt.__mro__])
show('inheritance', (issubclass(KeyboardInterrupt, BaseException),
                     issubclass(KeyboardInterrupt, Exception)))
for args in ((), ('interrupted',), ('interrupted', 42)):
    exc = KeyboardInterrupt(*args)
    show('constructed', (exc.args, str(exc), repr(exc),
                         isinstance(exc, BaseException), isinstance(exc, Exception)))
    show('links', (exc.__traceback__, exc.__cause__, exc.__context__, exc.__suppress_context__))

events = []
exc = KeyboardInterrupt('manual')
try:
    try:
        raise exc
    except Exception:
        raise AssertionError('Exception must not catch KeyboardInterrupt')
    finally:
        events.append('finally')
except BaseException as caught:
    show('caught BaseException', (caught is exc, type(caught).__name__, caught.args))
    gc.collect()
    show('retained traceback', caught.__traceback__ is not None)
show('cleanup', events)

try:
    raise KeyboardInterrupt
except KeyboardInterrupt as caught:
    show('raised class', (type(caught) is KeyboardInterrupt, caught.args))


class Interrupted(KeyboardInterrupt):
    pass


try:
    raise Interrupted('subclass')
except KeyboardInterrupt as caught:
    show('subclass', (type(caught).__name__, caught.args,
                      isinstance(caught, Exception), isinstance(caught, BaseException)))

payload = ['retained']
exc = KeyboardInterrupt(payload)
exc.add_note('first note')
exc.add_note('multi\nline')
gc.collect()
show('GC args', (exc.args[0] is payload, exc.args))
show('GC notes', exc.__notes__)
payload.append('updated')
show('live payload', str(exc))


class Arguments:
    def __iter__(self):
        gc.collect()
        yield payload
        gc.collect()
        yield 'replacement'


exc.args = Arguments()
gc.collect()
show('assigned args', (exc.args[0] is payload, exc.args))
show('notes preserved', exc.__notes__)
try:
    exc.add_note(42)
except TypeError:
    show('non-string note', 'TypeError')
else:
    raise AssertionError('notes require strings')

exc.detail = {'nested': payload}
gc.collect()
show('instance attributes', exc.detail)
