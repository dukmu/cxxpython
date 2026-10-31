import gc
import sys
import threading

barrier = threading.Barrier(2)
events = []
errors = []

def hook(event, args):
    if event == 'cxx.audit.parallel':
        barrier.wait(5)
        gc.collect()
        events.append(args[0])

sys.addaudithook(hook)

def worker(number):
    try:
        sys.audit('cxx.audit.parallel', number)
    except BaseException as error:
        errors.append(type(error).__name__)

threads = [threading.Thread(target=worker, args=(number,)) for number in range(2)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(5)
assert all(not thread.is_alive() for thread in threads)
assert not errors, errors
print('parallel hooks', sorted(events))
