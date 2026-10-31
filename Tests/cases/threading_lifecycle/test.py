import threading

# Derived from CPython 3.12.14 test_threading: joining inactive/current threads,
# enumerate after join, and exception-hook handling.
errors = []
events = []
ready = threading.Event()
release = threading.Event()


def worker():
    try:
        events.append((threading.current_thread().name,
                       threading.current_thread() in threading.enumerate()))
        ready.set()
        if not release.wait(5):
            raise RuntimeError("release timed out")
    except BaseException as exc:
        errors.append(type(exc).__name__)
        ready.set()


t = threading.Thread(target=worker, name="worker")
try:
    t.join()
except RuntimeError:
    print("inactive join rejected")
t.start()
assert ready.wait(5)
t.join(0.001)
print("running", t.is_alive(), t in threading.enumerate())
release.set()
t.join(5)
assert not t.is_alive() and not errors
print("worker", events, t in threading.enumerate())
try:
    threading.current_thread().join()
except RuntimeError:
    print("current join rejected")

old_hook = threading.excepthook
hook_events = []
try:
    def hook(args):
        hook_events.append((args.exc_type.__name__, str(args.exc_value), args.thread.name))

    threading.excepthook = hook
    def fail():
        raise ValueError("worker failure")

    failed = threading.Thread(target=fail, name="failing")
    failed.start()
    failed.join(5)
    assert not failed.is_alive()
finally:
    threading.excepthook = old_hook
print("hook", hook_events)
print("main", threading.current_thread() is threading.main_thread())
