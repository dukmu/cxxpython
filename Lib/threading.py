"""Thread objects built on the low-level _thread substrate."""

import _thread

__all__ = ['Thread', 'Lock', 'get_ident', 'current_thread']

Lock = _thread.allocate_lock
get_ident = _thread.get_ident

_main_thread = None


class Thread:
    """A small CPython-shaped thread wrapper."""

    def __init__(self, group=None, target=None, name=None, args=(), kwargs=None):
        if group is not None:
            raise ValueError('group argument must be None')
        self._target = target
        self._args = args
        if kwargs is None:
            kwargs = {}
        self._kwargs = kwargs
        self.name = name
        self.ident = None
        self._started = False
        self._done = Lock()

    def start(self):
        if self._started:
            raise ValueError('threads can only be started once')
        self._started = True
        self._done.acquire()
        self.ident = _thread.start_new_thread(self._bootstrap, ())

    def _bootstrap(self):
        try:
            if self._target is not None:
                self._target(*self._args, **self._kwargs)
        finally:
            self._done.release()

    def join(self):
        if not self._started:
            raise ValueError('cannot join thread before it is started')
        self._done.acquire()
        self._done.release()

    def is_alive(self):
        if not self._started:
            return False
        if self._done.acquire(False):
            self._done.release()
            return False
        return True


class _MainThread:
    def __init__(self):
        self.ident = get_ident()
        self.name = 'MainThread'

    def is_alive(self):
        return True


def current_thread():
    global _main_thread
    if _main_thread is None:
        _main_thread = _MainThread()
    return _main_thread
