import gc
import sys
import weakref


class Trace:
    def __call__(self, frame, event, arg):
        return self


def make_frame():
    return sys._getframe()


frame = make_frame()
trace = Trace()
reference = weakref.ref(trace)
frame.f_trace = trace
del trace
gc.collect()
print("retained", reference() is not None, frame.f_trace is reference())
frame.clear()
gc.collect()
print("cleared", frame.f_trace is None, reference() is None)
frame.clear()
print("repeat", frame.f_trace is None)
