import gc


names = ('msg', 'filename', 'lineno', 'offset', 'text', 'end_lineno',
         'end_offset', 'print_file_and_line')


def state(exc):
    return (exc.args, tuple(getattr(exc, name) for name in names))


for cls in (SyntaxError, IndentationError, TabError):
    for args in ((), ('message',), ('message', ('file.py', 2, 3, 'bad\n')),
                 ('message', ('file.py', 2, 3, 'bad\n', 2, 5)),
                 ('message', 1, 2)):
        print('construct', repr((cls.__name__, args, state(cls(*args)))))

exc = SyntaxError('original', ('file', 1, 2, 'line', 3, 4))
for index, name in enumerate(names):
    setattr(exc, name, ['changed', index])
print('assigned', repr(state(exc)))
exc.args = ('replacement',)
print('args assigned', repr(state(exc)))
for name in names:
    delattr(exc, name)
print('deleted', repr(state(exc)))

exc = SyntaxError('old', ('file', 1, 2, 'line', 3, 4))
for args in ((), ('new',), ('other', 1, 2), ('four', ('f', 10, 11, 't')),
             ('six', ['g', 20, 21, 'u', 22, 23])):
    exc.__init__(*args)
    print('reinit', repr((args, state(exc))))

for info in ((), (1, 2, 3), (1, 2, 3, 4, 5), (1, 2, 3, 4, 5, 6, 7), 42):
    exc = SyntaxError('old', ('file', 1, 2, 'line', 3, 4))
    try:
        exc.__init__('new', info)
    except Exception as error:
        print('bad info', repr((info, type(error).__name__, error.args, state(exc))))


class Location:
    def __iter__(self):
        gc.collect()
        for item in ('callback.py', 7, 8, 'line', 9, 10):
            gc.collect()
            yield item


exc = SyntaxError('callback', Location())
print('callback location', repr(tuple(getattr(exc, name) for name in names)))


class Broken:
    def __iter__(self):
        gc.collect()
        raise ValueError('location failed')


exc = SyntaxError('old', ('file', 1, 2, 'line', 3, 4))
try:
    exc.__init__('new', Broken())
except ValueError as error:
    print('callback failure', repr((error.args, exc.msg, exc.filename, exc.lineno,
                                    exc.end_lineno, exc.end_offset)))

try:
    compile('if True print(1)\n', 'sample.py', 'exec')
except SyntaxError as error:
    print('compile', repr(state(error)))

for source in ('x\0y', b'x\0y'):
    try:
        compile(source, 'nul.py', 'exec')
    except SyntaxError as error:
        print('nul', repr((type(source).__name__, state(error))))

exc = SyntaxError('initial', ('/some/path/file.py', 12, 1, 'line'))
print('str initial', repr(str(exc)))
for filename, lineno in (('dir/other.py', 4), ('/trailing/', None),
                         (None, 5), (7, True), ('name', 10 ** 100),
                         ('name', -(10 ** 100))):
    exc.filename = filename
    exc.lineno = lineno
    exc.msg = 'changed'
    print('str fields', repr((filename, lineno, str(exc))))


class IntSubclass(int):
    pass


exc.filename = None
exc.lineno = IntSubclass(7)
print('str int subclass', repr(str(exc)))
del exc.msg
print('str deleted msg', repr(str(exc)))


class Message:
    def __str__(self):
        gc.collect()
        exc.filename = 'mutated.py'
        exc.lineno = 99
        return 'from callback'


exc.msg = Message()
exc.filename = '/old/path.py'
exc.lineno = 13
print('str callback', repr((str(exc), exc.filename, exc.lineno)))


class BadMessage:
    def __str__(self):
        gc.collect()
        raise ValueError('str failed')


exc.msg = BadMessage()
try:
    str(exc)
except ValueError as error:
    print('str failure', repr(error.args))
