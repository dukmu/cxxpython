import _io
import gc


class Buffer(_io.BytesIO):
    def __init__(self, initial=b''):
        super().__init__(initial)
        self.events = []
        self.fail_write = False
        self.fail_flush = False
        self.fail_close = False
        self.interrupt = False
        self.saved = None

    def write(self, data):
        gc.collect()
        self.events.append(('write', len(data)))
        if self.interrupt:
            self.interrupt = False
            raise InterruptedError(4, 'interrupted')
        if self.fail_write:
            if self.fail_write == 'interrupted':
                raise InterruptedError('without errno')
            raise ValueError('write failed')
        return super().write(data)

    def flush(self):
        gc.collect()
        self.events.append(('flush',))
        if self.fail_flush:
            raise ValueError('flush failed')
        return super().flush()

    def close(self):
        gc.collect()
        self.events.append(('close',))
        if not self.closed:
            self.saved = self.getvalue()
        result = super().close()
        if self.fail_close:
            raise RuntimeError('close failed')
        return result


raw = Buffer()
text = _io.TextIOWrapper(raw, encoding='utf-8')
print('mixed count', text.write('text\n'))
raw.write(b'binary\n')
print('mixed before', raw.getvalue())
text.flush()
print('mixed after', raw.getvalue(), raw.events)
text.close()

for through in (False, True):
    raw = _io.BytesIO()
    binary = _io.BufferedWriter(raw)
    text = _io.TextIOWrapper(binary, encoding='utf-8', write_through=through)
    text.write('text\n')
    binary.write(b'binary\n')
    print('binary layer before', through, raw.getvalue())
    text.flush()
    print('binary layer after', through, raw.getvalue())
    text.close()

raw = Buffer()
text = _io.TextIOWrapper(raw, encoding='utf-8', write_through=True)
text.write('a')
text.write('b')
print('through', raw.getvalue(), raw.events)
text.flush()
print('through flush', raw.events)
text.close()

for newline in (None, '', '\r', '\r\n'):
    raw = Buffer()
    text = _io.TextIOWrapper(raw, encoding='utf-8', newline=newline, line_buffering=True)
    text.write('a')
    print('line before', repr(newline), raw.events)
    text.write('\r')
    text.write('b')
    text.write('\n')
    print('line after', raw.getvalue(), raw.events)
    text.close()

raw = Buffer()
text = _io.TextIOWrapper(raw, encoding='utf-8')
text.write('a' * 8191)
print('threshold before', raw.events)
text.write('b')
text.write('\u4e2d' * 2730)
text.write('\u4e2d')
text.write('x')
text.write('y' * 8192)
print('threshold after', raw.events, len(raw.getvalue()))
text.flush()
print('threshold flush', raw.events)
text.close()

raw = Buffer()
text = _io.TextIOWrapper(raw, encoding='utf-8')
print('empty', text.write(''), raw.events)
text.flush()
print('empty flush', raw.events)
text.close()

for operation in ('read', 'readline', 'seek', 'tell', 'detach', 'close'):
    raw = Buffer()
    text = _io.TextIOWrapper(raw, encoding='utf-8')
    text.write('abc')
    if operation == 'read':
        result = text.read(0)
    elif operation == 'readline':
        result = text.readline(0)
    elif operation == 'seek':
        result = text.seek(0)
    elif operation == 'tell':
        result = text.tell()
    elif operation == 'detach':
        result = text.detach() is raw
    else:
        result = text.close()
    data = raw.saved if raw.closed else raw.getvalue()
    print('barrier', operation, repr(result), data, raw.events)
    if operation != 'detach' and not raw.closed:
        text.close()
    elif operation == 'detach':
        raw.close()

raw = Buffer(b'0123')
text = _io.TextIOWrapper(raw, encoding='utf-8')
text.write('AB')
print('read data', repr(text.read()), raw.getvalue(), raw.events)
text.close()

for operation in ('flush', 'read', 'readline', 'seek', 'tell', 'detach', 'close'):
    raw = Buffer()
    text = _io.TextIOWrapper(raw, encoding='utf-8')
    raw.fail_write = True
    print('failure queued', operation, text.write('bad'), raw.events)
    try:
        if operation == 'read':
            text.read(0)
        elif operation == 'readline':
            text.readline(0)
        elif operation == 'seek':
            text.seek(0)
        elif operation == 'tell':
            text.tell()
        elif operation == 'detach':
            text.detach()
        elif operation == 'close':
            text.close()
        else:
            text.flush()
    except ValueError as error:
        print('failure', str(error), raw.events, text.closed)
    raw.fail_write = False
    if not raw.closed:
        print('still attached', text.buffer is raw)
        text.flush()
        print('retry', raw.getvalue(), raw.events)
        text.close()

raw = Buffer()
text = _io.TextIOWrapper(raw, encoding='utf-8', write_through=True)
raw.fail_write = True
try:
    text.write('bad')
except ValueError as error:
    print('through failure', str(error), raw.events)
raw.fail_write = False
text.flush()
print('through retry', raw.getvalue(), raw.events)
text.close()

raw = Buffer()
text = _io.TextIOWrapper(raw, encoding='utf-8')
text.write('prefix')
raw.fail_write = True
try:
    text.write('x' * 8192)
except ValueError as error:
    print('large failure', str(error), raw.events)
raw.fail_write = False
text.flush()
print('large retry', raw.getvalue(), raw.events)
text.close()

raw = Buffer()
text = _io.TextIOWrapper(raw, encoding='utf-8')
text.write('good')
raw.fail_flush = True
try:
    text.flush()
except ValueError as error:
    print('flush failure', str(error), raw.getvalue(), raw.events)
raw.fail_flush = False
text.flush()
print('flush retry', raw.getvalue(), raw.events)
text.close()

raw = Buffer()
text = _io.TextIOWrapper(raw, encoding='utf-8')
text.write('retry interrupted')
raw.interrupt = True
text.flush()
print('interrupted', raw.getvalue(), raw.events)
text.close()

raw = Buffer()
text = _io.TextIOWrapper(raw, encoding='utf-8')
text.write('no retry')
raw.fail_write = 'interrupted'
try:
    text.flush()
except InterruptedError as error:
    print('not EINTR', str(error), raw.events)
raw.fail_write = False
text.flush()
print('not EINTR retry', raw.getvalue(), raw.events)
text.close()

raw = Buffer()
text = _io.TextIOWrapper(raw, encoding='utf-8')
text.write('bad')
raw.fail_write = True
raw.fail_close = True
try:
    text.close()
except RuntimeError as error:
    print('close failure', str(error), str(error.__context__), raw.closed, raw.events)
