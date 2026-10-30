import sys

error_stream = sys.stderr


def unraisable(args, stream=error_stream):
    stream.write('flush failure\n')


class Broken:
    closed = False
    def write(self, value):
        return len(value)

    def flush(self):
        raise ValueError('flush failed')


sys.unraisablehook = unraisable
sys.stdout = Broken()
