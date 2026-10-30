for errno, cls in ((2, FileNotFoundError), (13, PermissionError), (17, FileExistsError), (20, NotADirectoryError), (21, IsADirectoryError), (4, InterruptedError), (11, BlockingIOError), (32, BrokenPipeError), (9, OSError)):
    e = OSError(errno, 'message', 'input', None, 'output')
    print(type(e) is cls, e.args, e.errno, e.strerror, e.filename, e.filename2, str(e))
e = OSError('one')
print(e.args, e.errno, e.strerror, e.filename, e.filename2, str(e))
e = OSError(2, 'first', 'x')
e.__init__(3, 'second', 'y')
print(e.args, e.errno, e.filename)
class Child(OSError):
    def __init__(self, *args):
        pass
e = Child(2, 'error', 'path')
print(type(e).__name__, e.errno, e.filename, e.args)
