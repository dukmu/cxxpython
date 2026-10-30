import _thread
import curses
import gc
import os


if os.environ.get("CURSES_FAILURE_CASE"):
    try:
        curses.wrapper(lambda window: None)
    except curses.error:
        print("CURSES_INIT_ERROR", flush=True)
    else:
        raise AssertionError("curses initialization unexpectedly succeeded")
else:
    failures = []
    done = _thread.allocate_lock()
    done.acquire()

    def worker():
        try:
            # Allow the main thread to enter the blocking terminal read.
            delay = _thread.allocate_lock()
            delay.acquire()
            assert not delay.acquire(timeout=0.2)
            assert sum(range(10000)) == 49995000
            gc.collect()
            print("CURSES_GC_PROGRESS", flush=True)
        except BaseException as error:
            failures.append(repr(error))
        finally:
            done.release()

    def application(window):
        try:
            window.move(-1, -1)
        except curses.error:
            pass
        else:
            raise AssertionError("invalid move did not raise curses.error")
        _thread.start_new_thread(worker, ())
        print("CURSES_GETCH_READY", flush=True)
        character = window.getch()
        done.acquire()
        assert not failures, failures
        assert character == ord("z"), character

    curses.wrapper(application)
    print("CURSES_INPUT_DONE", flush=True)
