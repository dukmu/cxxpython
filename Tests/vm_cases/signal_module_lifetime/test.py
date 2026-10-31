# The interpreter owns registered handlers; deleting/reimporting the module
# does not reset them as CPython 3.12's fresh native module state does.
import _signal
import gc
import sys

events = []
def handler(number, frame):
    events.append(number)

original = _signal.signal(_signal.SIGUSR1, handler)
del handler
del sys.modules['_signal']
del _signal
gc.collect()
import _signal
print('owned handler', _signal.getsignal(_signal.SIGUSR1).__name__)
_signal.raise_signal(_signal.SIGUSR1)
print('delivered after reimport', events == [_signal.SIGUSR1])
_signal.signal(_signal.SIGUSR1, original)
