import errno
import gc
import io
import os
import select
import signal
import socket

def check(label, operation):
    try:
        operation()
    except OSError as error:
        print(label, type(error).__name__, error.errno,
              error.filename, error.filename2, error.strerror)

check('open', lambda: os.open('missing-file', os.O_RDONLY))
check('bytes open', lambda: os.open(b'missing-file', os.O_RDONLY))
check('stat', lambda: os.stat('missing-file'))
check('io', lambda: io.FileIO('missing-file'))
check('rename', lambda: os.rename('missing-source', 'target'))
check('bytes rename', lambda: os.rename(b'missing-source', b'target'))
check('symlink', lambda: os.symlink('target', 'missing-directory/link'))
check('read', lambda: os.read(-1, 1))
check('write', lambda: os.write(-1, b'x'))
check('inheritable', lambda: os.get_inheritable(-1))
os.mkdir('directory')
check('mkdir', lambda: os.mkdir('directory'))
check('unlink', lambda: os.unlink('directory'))

sock = socket.socket()
fd = sock.fileno()
sock.close()
check('socket', lambda: sock.recv(1))
check('select', lambda: select.select([fd], [], [], 0))
check('signal', lambda: signal.signal(signal.SIGKILL, signal.SIG_IGN))

class MissingPath:
    def __fspath__(self):
        gc.collect()
        return 'collecting-path'

check('path callback', lambda: os.stat(MissingPath()))
try:
    socket.inet_ntop(-1, b'')
except ValueError as error:
    print('inet argument', str(error))

print(errno.ENOENT, errno.EBADF)
