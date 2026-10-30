import gc, sys
gc.disable()
captured = []
sys.unraisablehook = lambda a: captured.append(a)
class C:
    def __del__(self): raise ValueError('cleanup')
obj = C()
obj.cycle = obj
del obj
gc.collect()
cls = type(captured[0])
try:
    tuple.__new__(cls, ())
    rejected = False
except TypeError:
    rejected = True
print(rejected, repr(cls((None, None, None, None, None))))
try:
    sys.__unraisablehook__(None)
except TypeError as error:
    print(type(error).__name__)
