import gc
import threading
import types

failures = []
retained = []


def build_types():
    try:
        for index in range(12):
            class Published:
                __slots__ = ('value', '__weakref__')

                def __init__(self, value):
                    self.value = value

                @property
                def doubled(self):
                    return self.value * 2

            item = Published(index)
            assert item.doubled == index * 2
            retained.append(Published)
            gc.collect()
    except BaseException as error:
        failures.append((type(error).__name__, str(error)))


def inspect_types():
    try:
        for _ in range(12):
            for item in gc.get_objects():
                if isinstance(item, type):
                    assert isinstance(item.__name__, str)
                    assert isinstance(item.__qualname__, str)
                    assert item.__mro__[0] is item
                    assert isinstance(item.__dict__, types.MappingProxyType)
            gc.collect()
    except BaseException as error:
        failures.append((type(error).__name__, str(error)))


threads = [threading.Thread(target=build_types) for _ in range(2)]
threads.append(threading.Thread(target=inspect_types))
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
assert not any(thread.is_alive() for thread in threads), 'worker timed out'
assert not failures, failures
assert len(retained) == 24
print('publication', True)
