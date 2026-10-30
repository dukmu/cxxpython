import gc

error = BlockingIOError(11, 'busy', 4)
print(error.args, error.filename, error.characters_written, str(error))
error.characters_written = 8
print(error.characters_written)
del error.characters_written
print(hasattr(error, 'characters_written'))
try:
    del error.characters_written
except AttributeError as error:
    print(str(error))
class Index:
    def __index__(self):
        gc.collect()
        return 6
error = BlockingIOError(11, 'busy', Index())
print(error.characters_written)
error.characters_written = -2
print(error.characters_written)
error.characters_written = -1
print(hasattr(error, 'characters_written'))
for count in (1 << 100, 1.5):
    try:
        BlockingIOError(11, 'busy', count)
    except (TypeError, ValueError) as error:
        print(type(error).__name__)
class Derived(BlockingIOError):
    pass
error = Derived(11, 'busy', 3)
print(error.filename, error.args, hasattr(error, 'characters_written'))
error = OSError()
error.characters_written = 9
print(error.characters_written)
