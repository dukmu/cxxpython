import io
import sys

for name, saved in (('stdin', '__stdin__'), ('stdout', '__stdout__'),
                    ('stderr', '__stderr__')):
    stream = getattr(sys, name)
    print(name, type(stream).__module__, type(stream).__name__,
          stream is getattr(sys, saved), isinstance(stream, io.TextIOBase))
    print(stream.name, stream.mode, stream.encoding.lower().replace('-', '') == 'utf8',
          stream.errors, stream.buffer.raw.closefd)
    print(type(stream.buffer).__name__, stream.buffer.raw.name,
          stream.readable(), stream.writable(), stream.line_buffering)
sys.stdout.buffer.write(b'binary output\n')
sys.stderr.write('stderr surrogate: ' + chr(0xdcff) + '\n')
sys.stdout.write('buffered final line\n')
