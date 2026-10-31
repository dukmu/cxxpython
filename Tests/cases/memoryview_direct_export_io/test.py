import gc
import io

source = bytearray(b'----')
target = memoryview(source)
events = []


class Raw(io.RawIOBase):
    done = False

    def readable(self):
        return True

    def readinto(self, buffer):
        if self.done:
            return 0
        self.done = True
        gc.collect()
        try:
            target.release()
        except BufferError:
            events.append('pinned')
        else:
            raise AssertionError('released readinto target during raw callback')
        buffer[:4] = b'data'
        return 4


with io.BufferedReader(Raw(), 8) as reader:
    assert reader.readinto(target) == 4
assert source == b'data'
target.release()
source.extend(b'!')
print('scoped direct export', events, source)
