import gc
import os
import threading
import time


saved_tz = os.environ.get('TZ')
try:
    os.environ['TZ'] = 'UTC0'
    time.tzset()
    base = (2000, 1, 1, 0, 0, 0, 5, 1, 0)

    class Truth:
        def __bool__(self):
            gc.collect()
            assert time.gmtime(0).tm_year == 1970
            return True

    class Index:
        def __init__(self, value):
            self.value = value

        def __index__(self):
            assert bool(Truth())
            return self.value

        def __bool__(self):
            raise AssertionError('calendar fields use __index__, not __bool__')

    # Share both tuple fields and native tails while callbacks collect/re-enter.
    shared = time.struct_time(tuple(Index(value) for value in base),
                              {'tm_zone': 'SHARED', 'tm_gmtoff': Index(5400)})
    timestamp = Index(946684800)
    gate = threading.Barrier(4)
    errors = []
    completed = [False] * 4
    results = [0] * 4

    def worker(index):
        try:
            gate.wait(5)
            for item in range(4):
                utc = time.gmtime(timestamp)
                local = time.localtime(timestamp)
                assert utc == local == base
                assert utc.tm_zone == 'GMT' and utc.tm_gmtoff == 0
                assert local.tm_zone == 'UTC' and local.tm_gmtoff == 0
                assert time.strftime('%Y-%m-%d %Z %z', shared) == '2000-01-01 SHARED +0130'
                assert time.asctime(shared) == 'Sat Jan  1 00:00:00 2000'
                assert time.mktime(shared) == 946684800.0
                assert time.ctime(timestamp) == 'Sat Jan  1 00:00:00 2000'
                gc.collect()
                assert shared.tm_zone == 'SHARED'
                results[index] += 1
        except BaseException as exc:
            errors.append((index, type(exc).__name__, str(exc)))
        finally:
            completed[index] = True

    threads = [threading.Thread(target=worker, args=(index,)) for index in range(4)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join(5)
    assert not any(thread.is_alive() for thread in threads)
    assert not errors, errors
    assert all(completed), completed
    print('parallel calendar queries', results, completed)

    class RaisingTruth:
        def __bool__(self):
            gc.collect()
            raise RuntimeError('bool callback failed')

    class RaisingIndex:
        def __index__(self):
            return int(bool(RaisingTruth()))

    for function in (time.asctime, time.mktime):
        try:
            function((RaisingIndex(),) + base[1:])
        except RuntimeError as exc:
            print('callback exception', function.__name__, str(exc))
        else:
            raise AssertionError('bool callback exception must propagate')
    assert time.strftime('%Y', time.gmtime(0)) == '1970'
    print('queries recover after callbacks')
finally:
    if saved_tz is None:
        del os.environ['TZ']
    else:
        os.environ['TZ'] = saved_tz
    time.tzset()
