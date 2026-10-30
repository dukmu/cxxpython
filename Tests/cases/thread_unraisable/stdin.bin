import _thread
import sys

done = _thread.allocate_lock()
done.acquire()
seen = []
def hook(info):
    seen.append((info.exc_type.__name__, info.object.__name__, info.err_msg))
    done.release()
sys.unraisablehook = hook
def fail():
    raise ValueError('worker failure')
_thread.start_new_thread(fail, ())
done.acquire()
print(seen[0])
done.release()
