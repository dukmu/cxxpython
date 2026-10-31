import atexit
import gc
import threading

release = threading.Event()
done = []

def worker():
    assert release.wait(5)
    gc.collect()
    done.append('joined')
    print('worker finished')

def at_exit():
    assert done == ['joined']
    print('atexit after join')

threading.Thread(target=worker).start()
atexit.register(at_exit)
print('main finished')
release.set()
