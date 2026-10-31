import _thread
import gc
import os
import sys

os.mkdir('entries')
names = ['file' + str(i) for i in range(32)]
for name in names:
    with open('entries/' + name, 'w') as stream:
        stream.write('data')
try:
    shared = os.scandir('entries')
    reference_next = _thread.allocate_lock() if sys.implementation.name == 'cpython' else None
    results = [[], [], [], []]
    errors = []
    done = [_thread.allocate_lock() for i in range(4)]
    for lock in done:
        lock.acquire()

    def consume(index):
        try:
            while True:
                # The pinned reference releases its GIL around readdir on the
                # shared DIR pointer; only its next calls need serialization.
                try:
                    if reference_next is None:
                        entry = next(shared)
                    else:
                        with reference_next:
                            entry = next(shared)
                except StopIteration:
                    break
                gc.collect()
                assert entry.stat().st_size == 4
                results[index].append(entry.name)
        except BaseException as exc:
            errors.append(type(exc).__name__)
        finally:
            done[index].release()

    for i in range(4):
        _thread.start_new_thread(consume, (i,))
    for lock in done:
        lock.acquire()
    print('shared iterator', not errors, sorted(sum(results, [])) == sorted(names))
    shared.close()
    with os.scandir('entries') as iterator:
        entry = next(iterator)
    ready = [_thread.allocate_lock(), _thread.allocate_lock()]
    for lock in ready:
        lock.acquire()

    class Follow:
        def __init__(self, index):
            self.index = index

        def __bool__(self):
            ready[self.index].release()
            other = ready[1 - self.index]
            other.acquire()
            other.release()
            gc.collect()
            return True

    cached = [None, None]

    def cache(index):
        try:
            cached[index] = entry.stat(follow_symlinks=Follow(index))
        except BaseException as exc:
            errors.append(type(exc).__name__)
        finally:
            done[index].release()

    for i in range(2):
        _thread.start_new_thread(cache, (i,))
    for lock in done[:2]:
        lock.acquire()
    print('shared cache', not errors, cached[0] is cached[1], cached[0].st_size == 4)
finally:
    for name in names:
        os.unlink('entries/' + name)
    os.rmdir('entries')
