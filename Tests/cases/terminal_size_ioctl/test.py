import errno
import os

fd = os.open(__file__, os.O_RDONLY)
try:
    try:
        os.get_terminal_size(fd)
    except OSError as exc:
        print(exc.errno == errno.ENOTTY)
finally:
    os.close(fd)
try:
    os.get_terminal_size(-1)
except OSError as exc:
    print(exc.errno == errno.EBADF)

class Descriptor:
    def __index__(self):
        return -1

try:
    os.get_terminal_size(Descriptor())
except OSError as exc:
    print(exc.errno == errno.EBADF)
