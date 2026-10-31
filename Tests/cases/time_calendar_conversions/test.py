import gc
import os
import time


def error(label, function, *args):
    try:
        function(*args)
    except (TypeError, ValueError, OverflowError, RuntimeError) as exc:
        print(label, type(exc).__name__, str(exc))
    else:
        raise AssertionError(label)


saved_tz = os.environ.get('TZ')
try:
    os.environ['TZ'] = 'UTC0'
    time.tzset()
    for seconds in (-1.25, -1.0, -0.001, -0.0, 0.999, 1.999,
                    946684800, 946684800.25, False, True):
        utc = time.gmtime(seconds)
        local = time.localtime(seconds)
        assert utc == local
        assert time.ctime(seconds) == time.asctime(local)
        print('floor', repr(seconds), tuple(utc), utc.tm_zone, utc.tm_gmtoff)

    # Bracket current-time calls without printing wall-clock-dependent values.
    for function in (time.gmtime, time.localtime, time.ctime):
        before = int(time.time())
        implicit = function()
        explicit = function(None)
        after = int(time.time())
        if function is time.ctime:
            choices = [time.ctime(second) for second in range(before, after + 1)]
            assert implicit in choices and explicit in choices
        else:
            assert before <= time.mktime(implicit) <= after
            assert before <= time.mktime(explicit) <= after
        print('current', function.__name__, type(implicit) is type(explicit))

    calls = []

    class Index:
        def __index__(self):
            calls.append('index')
            gc.collect()
            assert time.gmtime(0).tm_year == 1970
            return -1

        def __bool__(self):
            raise AssertionError('timestamp truth value must not be queried')

    class FloatOnly:
        def __float__(self):
            raise AssertionError('__float__ must not be used')

    class BadIndex:
        def __index__(self):
            gc.collect()
            return 'not an integer'

    class RaisingIndex:
        def __index__(self):
            gc.collect()
            raise RuntimeError('index callback failed')

    class FloatSubclass(float):
        def __float__(self):
            raise AssertionError('float subclass value must be read directly')

    class IntSubclass(int):
        def __index__(self):
            raise AssertionError('int subclass value must be read directly')

    for function in (time.gmtime, time.localtime, time.ctime):
        assert function(Index()) == function(-1)
        assert function(FloatSubclass(-0.25)) == function(-1)
        assert function(IntSubclass(1)) == function(1)
        for label, value in [('text', '0'), ('float-only', FloatOnly()),
                             ('bad-index', BadIndex()), ('raised-index', RaisingIndex()),
                             ('nan', float('nan')), ('inf', float('inf')),
                             ('negative-inf', -float('inf')), ('huge-float', 1e200),
                             ('negative-huge-float', -1e200), ('huge-int', 1 << 100),
                             ('negative-huge-int', -(1 << 100))]:
            error(function.__name__ + ' ' + label, function, value)
    print('collecting index calls', calls)
finally:
    if saved_tz is None:
        del os.environ['TZ']
    else:
        os.environ['TZ'] = saved_tz
    time.tzset()
