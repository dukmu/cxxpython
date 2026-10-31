import _thread
import gc
import sys

name = "long_\u03bb_\U0001f40d_" + "x" * 128
equal_name = (name + "!")[:-1]
table = {name: "first"}
table[equal_name] = "second"
print(next(iter(table)) is name, table[name], len(table))

events = []
class Key(str):
    def __hash__(self):
        events.append("hash")
        gc.collect()
        return str.__hash__(self)
    def __eq__(self, other):
        events.append("eq")
        gc.collect()
        table["side"] = 17
        return str.__eq__(self, other)

original = Key(name)
probe = Key(equal_name)
table = {original: "old"}
table[probe] = "new"
print(next(iter(table)) is original, table["side"], table[original], events.count("hash"), events.count("eq"))
copied = table.copy()
view = table.keys()
table.clear()
gc.collect()
print(len(view), next(iter(copied)) is original, copied[original])

class Namespace:
    pass
setattr(Namespace, name, "class")
class_copy = Namespace.__dict__.copy()
delattr(Namespace, name)
sys.__dict__[name] = "module"
module_copy = sys.__dict__.copy()
del sys.__dict__[name]
gc.collect()
print(class_copy[name], module_copy[name], name in Namespace.__dict__, name in sys.__dict__)

shared = {}
errors = []
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in done:
    lock.acquire()

def worker(number):
    try:
        for i in range(30):
            key = name + str(number) + "_" + str(i)
            shared[key] = i
            if shared[key] != i:
                errors.append("value")
            if i % 10 == 0:
                gc.collect()
    except Exception:
        errors.append("exception")
    finally:
        done[number].release()

for number in range(2):
    _thread.start_new_thread(worker, (number,))
for lock in done:
    lock.acquire()
gc.collect()
print(len(shared), len(errors), all(shared[name + str(n) + "_" + str(i)] == i for n in range(2) for i in range(30)))
