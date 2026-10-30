import sys
class Stream:
    def __init__(self): self.parts = []
    def write(self, text): self.parts.append(text)
    def flush(self): pass
stream = Stream()
sys.stderr = stream
try:
    raise RuntimeError('inner cause')
except RuntimeError as inner:
    try:
        raise ValueError('outer') from inner
    except ValueError as outer:
        sys.__excepthook__(type(outer), outer, outer.__traceback__)
text = ''.join(stream.parts)
print('RuntimeError: inner cause' in text, 'direct cause of the following exception' in text,
      text.find('RuntimeError: inner cause') < text.find('ValueError: outer'))
stream.parts.clear()
try:
    raise RuntimeError('context')
except RuntimeError:
    try:
        raise ValueError('outer')
    except ValueError as outer:
        sys.__excepthook__(type(outer), outer, outer.__traceback__)
        print('During handling of the above exception' in ''.join(stream.parts))
        stream.parts.clear()
        outer.__suppress_context__ = True
        sys.__excepthook__(type(outer), outer, outer.__traceback__)
        print('RuntimeError: context' in ''.join(stream.parts))
