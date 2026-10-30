import gc

error = ValueError('value')
print(hasattr(error, '__notes__'))
print(error.add_note('first'))
error.add_note('second')
print(error.__notes__)
error.__notes__ = None
try:
    error.add_note('bad')
except TypeError as failure:
    print(str(failure))
del error.__notes__
try:
    error.add_note(3)
except TypeError:
    print('note type')
class Custom(ValueError):
    def __setattr__(self, name, value):
        gc.collect()
        super().__setattr__(name, value)
custom = Custom()
custom.add_note('callback')
print(custom.__notes__)
