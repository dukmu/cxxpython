import _thread
import gc

entered = _thread.allocate_lock()
release = _thread.allocate_lock()
done = _thread.allocate_lock()
for lock in (entered, release, done):
    lock.acquire()
finalized = []
results = []

class Pause:
    def __await__(self):
        entered.release()
        release.acquire()
        yield 'pause'

async def values():
    try:
        yield 1
        await Pause()
        yield 2
    finally:
        finalized.append(True)

def drive(request):
    try:
        return request.send(None)
    except StopIteration as error:
        return 'value', error.value

g = values()
print(drive(g.__anext__()))
request = g.asend(None)
def worker():
    try:
        results.append(request.send(None))
    except BaseException as error:
        results.append(type(error).__name__)
    finally:
        done.release()

_thread.start_new_thread(worker, ())
entered.acquire()
gc.collect()
print(g.ag_running)
try:
    g.__anext__().send(None)
except RuntimeError:
    print('busy')
release.release()
done.acquire()
print(results)
gc.collect()
print(drive(request))
print(drive(g.aclose()))
print(finalized, g.ag_running)
