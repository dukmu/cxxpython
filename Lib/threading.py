"""Thread objects built on the low-level _thread substrate."""

import _thread

__all__ = [
    'Thread',
    'Lock',
    'RLock',
    'Condition',
    'Event',
    'get_ident',
    'current_thread',
]

Lock = _thread.allocate_lock
get_ident = _thread.get_ident

_main_thread = None


class RLock:
    """A reentrant lock owned by one thread at a time."""

    def __init__(self):
        self._block = Lock()
        self._owner = None
        self._count = 0

    def acquire(self, blocking=True):
        ident = get_ident()
        if self._owner == ident:
            self._count = self._count + 1
            return True
        acquired = self._block.acquire(blocking)
        if acquired:
            self._owner = ident
            self._count = 1
        return acquired

    def release(self):
        if self._owner != get_ident():
            raise ValueError('cannot release un-acquired lock')
        self._count = self._count - 1
        if self._count == 0:
            self._owner = None
            self._block.release()

    def locked(self):
        return self._count > 0

    def _is_owned(self):
        return self._owner == get_ident()

    def _release_save(self):
        if self._owner != get_ident():
            raise ValueError('cannot release un-acquired lock')
        state = (self._count, self._owner)
        self._count = 0
        self._owner = None
        self._block.release()
        return state

    def _acquire_restore(self, state):
        self._block.acquire()
        self._count = state[0]
        self._owner = state[1]

    def __enter__(self):
        self.acquire()
        return self

    def __exit__(self, exc_type, exc, traceback):
        self.release()
        return False


class Condition:
    """A condition variable implemented with primitive locks."""

    def __init__(self, lock=None):
        if lock is None:
            lock = RLock()
        self._lock = lock
        self._waiters = []

    def acquire(self, blocking=True):
        return self._lock.acquire(blocking)

    def release(self):
        return self._lock.release()

    def _is_owned(self):
        if hasattr(self._lock, '_is_owned'):
            return self._lock._is_owned()
        if self._lock.acquire(False):
            self._lock.release()
            return False
        return True

    def _release_save(self):
        if hasattr(self._lock, '_release_save'):
            return self._lock._release_save()
        self._lock.release()
        return None

    def _acquire_restore(self, state):
        if hasattr(self._lock, '_acquire_restore'):
            self._lock._acquire_restore(state)
        else:
            self._lock.acquire()

    def wait(self, timeout=None):
        if timeout is not None:
            raise ValueError('timeout is not supported yet')
        if not self._is_owned():
            raise ValueError('cannot wait on un-acquired lock')
        waiter = Lock()
        waiter.acquire()
        self._waiters.append(waiter)
        saved = self._release_save()
        try:
            waiter.acquire()
        finally:
            self._acquire_restore(saved)
        return True

    def notify(self, n=1):
        if not self._is_owned():
            raise ValueError('cannot notify on un-acquired lock')
        if n <= 0:
            return None
        remaining = []
        count = 0
        for waiter in self._waiters:
            if count < n:
                waiter.release()
                count = count + 1
            else:
                remaining.append(waiter)
        self._waiters = remaining
        return None

    def notify_all(self):
        return self.notify(len(self._waiters))

    def __enter__(self):
        self.acquire()
        return self

    def __exit__(self, exc_type, exc, traceback):
        self.release()
        return False


class Event:
    """A boolean event flag with condition-variable notification."""

    def __init__(self):
        self._cond = Condition(Lock())
        self._flag = False

    def is_set(self):
        return self._flag

    def set(self):
        with self._cond:
            self._flag = True
            self._cond.notify_all()

    def clear(self):
        with self._cond:
            self._flag = False

    def wait(self, timeout=None):
        if timeout is not None:
            raise ValueError('timeout is not supported yet')
        with self._cond:
            while not self._flag:
                self._cond.wait()
            return True


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
