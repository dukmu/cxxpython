import _thread
import gc
import sys

done = _thread.allocate_lock()
done.acquire()
observed = []


def trace(frame, event, arg):
    if frame.f_code.co_name == "work" and event == "return":
        observed.append(arg)
    return trace


def work():
    gc.collect()
    return 17


def worker():
    observed.append(sys.gettrace() is None)
    sys.settrace(trace)
    work()
    sys.settrace(None)
    done.release()


sys.settrace(trace)
_thread.start_new_thread(worker, ())
done.acquire()
print(observed, sys.gettrace() is trace)
sys.settrace(None)
