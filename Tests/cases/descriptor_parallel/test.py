import _thread
import gc

for factory in (staticmethod, classmethod):
    wrapper = factory(lambda *args: 1)
    class Shared:
        method = wrapper
    done = [_thread.allocate_lock(), _thread.allocate_lock()]
    for lock in done:
        lock.acquire()
    errors = []
    def writer():
        try:
            for i in range(80):
                wrapper.__init__(lambda *args: 2)
                gc.collect()
                wrapper.__init__(lambda *args: 1)
        except Exception as error:
            errors.append(type(error).__name__)
        finally:
            done[0].release()
    def reader():
        try:
            for i in range(160):
                assert Shared.method() in (1, 2)
                gc.collect()
        except Exception as error:
            errors.append(type(error).__name__)
        finally:
            done[1].release()
    _thread.start_new_thread(writer, ())
    _thread.start_new_thread(reader, ())
    for lock in done:
        lock.acquire()
    print(errors)
