import io
import sys

print(type(io))
print(io.TextIOWrapper)
print(type(sys.stdout))
print(sys.stdout.write('io-write\n'))
print(sys.stdout.flush())

stream = io.open('/tmp/cxxpython_io_module_fixture.txt', 'w')
print(type(stream))
print(stream.write('alpha\nbeta\n'))
print(stream.close())
print(stream.closed())

with io.open('/tmp/cxxpython_io_context_fixture.txt', 'w') as writer:
    print(writer.write('context\n'))
    print(writer.closed())
print(writer.closed())

with io.open('/tmp/cxxpython_io_context_fixture.txt') as context_reader:
    print(context_reader.read())
print(context_reader.closed())

reader = io.open('/tmp/cxxpython_io_module_fixture.txt')
print(reader.readline())
print(reader.read())
reader.close()

try:
    reader.read()
except ValueError as exc:
    print(type(exc))

try:
    io.open('/definitely/not/a/cxxpython/io/path')
except OSError as exc:
    print(type(exc))
