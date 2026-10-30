class Bad:
    def __getnewargs_ex__(self):
        return ((), [])
try:
    Bad().__reduce_ex__(4)
except TypeError:
    print('bad newargs')
class BadArgs:
    def __getnewargs__(self):
        return []
try:
    BadArgs().__reduce_ex__(4)
except TypeError:
    print('bad args')
try:
    object().__reduce_ex__(1 << 80)
except OverflowError:
    print('protocol overflow')
class Fail:
    def __getstate__(self):
        raise ValueError('state')
try:
    Fail().__reduce_ex__(3)
except ValueError as error:
    print(str(error))
class Slotted:
    __slots__ = ('x',)
try:
    Slotted().__reduce_ex__(0)
except TypeError:
    print('old protocol slots')
