import gc, sys
gc.disable()
events = []
class Stream:
    def write(self, text):
        events.append(text)
    def flush(self): pass
sys.stderr = Stream()
def badhook(args): raise RuntimeError('hook failed')
sys.unraisablehook = badhook
class C:
    def __del__(self): raise ValueError('cleanup')
obj = C()
obj.cycle = obj
del obj
gc.collect()
text = ''.join(events)
print('Exception ignored in sys.unraisablehook' in text,
      'RuntimeError: hook failed' in text)
class FailingStream:
    def write(self, text): raise RuntimeError('write failed')
    def flush(self): pass
sys.stderr = FailingStream()
obj = C()
obj.cycle = obj
del obj
gc.collect()
sys.stderr = None
obj = C()
obj.cycle = obj
del obj
gc.collect()
print('collection completed')
