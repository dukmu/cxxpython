import gc
import copyreg

class Plain:
    pass
item = Plain()
print(item.__getstate__())
item.x = 3
print(item.__getstate__(), item.__getstate__() is item.__dict__)
constructor, arguments, state, items, pairs = item.__reduce_ex__(4)
print(constructor is copyreg.__newobj__, arguments == (Plain,), state, items, pairs)
print(type(constructor(*arguments)) is Plain)

class Slotted:
    __slots__ = ['x', '__private', '__dict__']
    def __init__(self):
        self.x = 4
        self.__private = 5
        self.z = 6
value = Slotted()
print(value.__getstate__())
print(value.__reduce_ex__(2)[2])

class NewArgs:
    def __getnewargs_ex__(self):
        gc.collect()
        return ((3,), {'kw': 4})
    def __getstate__(self):
        gc.collect()
        return ('custom', 7)
result = NewArgs().__reduce_ex__(4)
print(result[0] is copyreg.__newobj_ex__, result[1] == (NewArgs, (3,), {'kw': 4}), result[2])
class Custom:
    def __reduce__(self):
        gc.collect()
        return 'custom reduce'
print(Custom().__reduce_ex__(4))

class PlainList(list):
    pass
result = PlainList([1, 2]).__reduce_ex__(3)
print(list(result[3]), result[4])
class PlainDict(dict):
    pass
result = PlainDict(a=1).__reduce_ex__(5)
print(result[3], list(result[4]))
result = Plain().__reduce_ex__(0)
print(result[0] is copyreg._reconstructor, result[1] == (Plain, object, None))
