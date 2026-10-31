import _thread
import gc
from dataclasses import asdict, dataclass, field, replace


callbacks = []


class DefaultDescriptor:
    def __set_name__(self, owner, name):
        gc.collect()
        callbacks.append('set_name')
        self.name = name

    def __get__(self, instance, owner):
        gc.collect()
        callbacks.append('get')
        if instance is None:
            return 8
        return instance.__dict__[self.name]

    def __set__(self, instance, value):
        instance.__dict__[self.name] = value


@dataclass
class Callback:
    value: int = DefaultDescriptor()


print(Callback().value, 'set_name' in callbacks, 'get' in callbacks)


def factory():
    gc.collect()
    return [1]


@dataclass
class Shared:
    value: int
    items: list = field(default_factory=factory)

    def __post_init__(self):
        gc.collect()


shared = Shared(5)
errors = []
completed = []
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in done:
    lock.acquire()


def worker(index):
    try:
        for _ in range(5):
            @dataclass
            class Local:
                value: int
                items: list = field(default_factory=factory)

                def __post_init__(self):
                    gc.collect()

            value = Local(index)
            assert asdict(value) == {'value': index, 'items': [1]}
            clone = replace(shared, value=index)
            assert clone.items is shared.items
            assert clone.value == index
            gc.collect()
            completed.append(index)
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        done[index].release()


for index in range(2):
    _thread.start_new_thread(worker, (index,))
for lock in done:
    lock.acquire()
print(sorted(errors), completed.count(0), completed.count(1), shared.items)
