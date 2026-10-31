import gc
import signal
import unittest

original = signal.getsignal(signal.SIGINT)
result = unittest.TestResult()
unittest.registerResult(result)
try:
    signal.signal(signal.SIGINT, signal.default_int_handler)
    unittest.installHandler()
    installed = signal.getsignal(signal.SIGINT)
    gc.collect()
    signal.raise_signal(signal.SIGINT)
    print('first interrupt', result.shouldStop, installed.called)
    try:
        signal.raise_signal(signal.SIGINT)
    except KeyboardInterrupt as error:
        print('second interrupt', error.args)
    print('remove result', unittest.removeResult(result), unittest.removeResult(result))

    @unittest.removeHandler
    def without_handler():
        print('temporarily removed', signal.getsignal(signal.SIGINT) is signal.default_int_handler)
        return 17

    print('decorated', without_handler(), signal.getsignal(signal.SIGINT) is installed)
    unittest.removeHandler()
    print('restored default', signal.getsignal(signal.SIGINT) is signal.default_int_handler)
finally:
    unittest.removeResult(result)
    signal.signal(signal.SIGINT, original)
