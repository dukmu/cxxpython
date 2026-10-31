import os
import time


saved_tz = os.environ.get('TZ')
try:
    for zone in ('UTC0', 'EST5EDT,M3.2.0/2,M11.1.0/2'):
        os.environ['TZ'] = zone
        time.tzset()
        print('TZ', zone, time.timezone, time.altzone, time.daylight, time.tzname)
        for seconds in (-1, 0, 1577880000, 1593604800,
                        1583650799, 1583650800, 1604210399, 1604210400):
            local = time.localtime(seconds)
            assert time.mktime(local) == seconds
            assert time.mktime(tuple(local)) == seconds
            assert time.ctime(seconds) == time.asctime(local)
            print('local', seconds, tuple(local), local.tm_zone, local.tm_gmtoff,
                  time.strftime('%Z %z', local))

    # The repeated autumn hour is disambiguated by tm_isdst, not by tail fields.
    for dst in (1, 0):
        repeated = (2020, 11, 1, 1, 30, 0, 0, 0, dst)
        seconds = time.mktime(repeated)
        print('fall back', dst, seconds, time.localtime(seconds).tm_isdst)
    gap = (2020, 3, 8, 2, 30, 0, 0, 0, 0)
    seconds = time.mktime(gap)
    print('spring gap', seconds, tuple(time.localtime(seconds)))

    os.environ['TZ'] = 'UTC0'
    time.tzset()
    # mktime normalizes these fields instead of applying strftime's checks.
    for value in [(2000, 13, 1, 0, 0, 0, 0, 0, -1),
                  (2000, 3, 0, 0, 0, 0, 0, 0, -1),
                  (2000, 1, 1, 24, 61, 61, 0, 0, -1),
                  (1970, 1, 1, 0, 0, -1, 0, 0, -1)]:
        seconds = time.mktime(value)
        assert type(seconds) is float
        print('normalized', seconds, tuple(time.localtime(seconds)))
    print('restored UTC', time.timezone, time.altzone, time.daylight, time.tzname)
finally:
    if saved_tz is None:
        del os.environ['TZ']
    else:
        os.environ['TZ'] = saved_tz
    time.tzset()
