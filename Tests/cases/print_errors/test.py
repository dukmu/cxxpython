import gc
import sys

try:
    sys.stdout.write(chr(0xd800))
except UnicodeEncodeError as error:
    assert error.object == chr(0xd800)
    assert error.start == 0 and error.end == 1
else:
    assert False
events = []


class Flag:
    def __bool__(self):
        gc.collect()
        events.append('bool')
        raise ValueError('flush')


try:
    print('never written', sep=42, flush=Flag())
except ValueError:
    pass
else:
    assert False
assert events == ['bool']
print('print errors ok')
