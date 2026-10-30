import _thread
import gc
import readline

ready = _thread.allocate_lock()
ready.acquire()
done = _thread.allocate_lock()
done.acquire()
failures = []


def before_input():
    ready.release()


def worker():
    ready.acquire()
    try:
        gc.collect()
        assert sum(range(1000)) == 499500
        print("GC_PROGRESS", flush=True)
        readline.add_history("from worker")
    except BaseException as error:
        failures.append(type(error).__name__)
    finally:
        done.release()


readline.set_pre_input_hook(before_input)
_thread.start_new_thread(worker, ())
print("RESULT", input("gc> "))
done.acquire()
readline.set_pre_input_hook(None)
assert not failures, failures
print("THREAD_DONE", flush=True)
