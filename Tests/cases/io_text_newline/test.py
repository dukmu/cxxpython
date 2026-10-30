import _io
import codecs

d = _io.IncrementalNewlineDecoder(codecs.getincrementaldecoder('utf-8')(), True)
print(repr(d.decode(b'a\r')))
state = d.getstate()
print(repr(d.decode(b'\nb\rc\n')))
print(repr(d.newlines))
d.setstate(state)
print(repr(d.decode(b'\nx', True)))
d.reset()
print(repr(d.newlines))
print(repr(d.decode(b'\xe4\xb8')))
print(repr(d.decode(b'\xad\r', True)))

d = _io.IncrementalNewlineDecoder(None, False)
print(repr(d.decode('a\r')))
print(repr(d.decode('\nb\rc\n', True)))
print(repr(d.newlines))
