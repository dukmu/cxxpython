import _thread
import gc
import readline

gc.disable()
done = _thread.allocate_lock()
done.acquire()
sleep = _thread.allocate_lock()
sleep.acquire()
errors = []


def worker():
    try:
        while True:
            try:
                with open("collect_go"):
                    pass
                break
            except FileNotFoundError:
                pass
            sleep.acquire(timeout=0.01)
        gc.collect()
        with open("collect_done", "w") as stream:
            stream.write("done")
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        done.release()


_thread.start_new_thread(worker, ())
assert input(("x" * 80 + "\n") * 1024 + "ready> ") == "answer"
done.acquire()
assert not errors, errors
print("BACKPRESSURE_DONE", flush=True)
