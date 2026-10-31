import os
import time


def error(label, function, *args):
    try:
        function(*args)
    except (TypeError, ValueError, OverflowError, UnicodeError) as exc:
        print(label, type(exc).__name__, str(exc))
    else:
        raise AssertionError(label)


# Special strings and field bounds follow CPython 3.12.14 test_time.py.
saved_tz = os.environ.get('TZ')
try:
    os.environ['TZ'] = 'UTC0'
    time.tzset()
    utc = time.gmtime(946684800)
    print('UTC', time.strftime('%Y-%m-%d %H:%M:%S %j %w %Z %z %%', utc))
    print('asctime', time.asctime(utc), time.ctime(946684800))
    for fmt in ('', '\0', '\0%Y\0%m', '%Y\0%m\0',
                '\U0001f40d', '\U0001f4bb%Y\U0001f40d%m',
                '\ud83d', '\udc0d', '\ud83d%Y\udc0d%m',
                '%Y\ud83d\udc0d%m', '\udcf0\udc9f\udc90\udc8d%Y'):
        print('special', ascii(time.strftime(fmt, utc)))
    assert time.strftime('\0' * 1000, utc) == '\0' * 1000
    text = time.strftime('%c', utc)
    month = time.strftime('%B', utc)
    assert time.strftime('\ud83d%c\udc0d%B', utc) == '\ud83d' + text + '\udc0d' + month
    assert time.strftime('%c\0%B\0', utc) == text + '\0' + month + '\0'
    print('long NUL and locale text preserved')
    custom = time.struct_time(tuple(utc), {'tm_zone': 'CUSTOM', 'tm_gmtoff': 5400})
    print('native tails', time.strftime('%Z %z', custom))
    print('zero fields', time.strftime('%Y %m %d %H %M %S %w %j', (2000,) + (0,) * 8))
    print('leap seconds', time.strftime('%S', (2000, 1, 1, 0, 0, 60, 5, 1, 0)),
          time.strftime('%S', (2000, 1, 1, 0, 0, 61, 5, 1, 0)))

    base = (2000, 1, 1, 0, 0, 0, 5, 1, -1)
    fields = [(1, 'month', (-1, 13)), (2, 'day', (-1, 32)),
              (3, 'hour', (-1, 24)), (4, 'minute', (-1, 60)),
              (5, 'second', (-1, 62)), (6, 'weekday', (-2,)),
              (7, 'yearday', (-1, 367))]
    for function in (time.asctime, time.strftime):
        def format_tuple(value):
            if function is time.strftime:
                return function('', value)
            return function(value)

        for position, name, values in fields:
            for value in values:
                invalid = list(base)
                invalid[position] = value
                error(function.__name__ + ' ' + name + ' ' + str(value),
                      format_tuple, tuple(invalid))
        for position, values in [(1, (0, 12)), (2, (0, 31)), (3, (23,)),
                                 (4, (59,)), (5, (60, 61)), (7, (0, 366))]:
            for value in values:
                valid = list(base)
                valid[position] = value
                assert type(format_tuple(tuple(valid))) is str
        assert format_tuple(base[:6] + (-1,) + base[7:]) == format_tuple(base[:6] + (6,) + base[7:])
        error(function.__name__ + ' short', format_tuple, base[:8])
        error(function.__name__ + ' long', format_tuple, base + (0,))
        error(function.__name__ + ' list', format_tuple, list(base))
        error(function.__name__ + ' None', format_tuple, None)
        error(function.__name__ + ' float field', format_tuple, (2000.0,) + base[1:])
    error('format bytes', time.strftime, b'%Y', utc)
    for year in (2147483647, -2147481748):
        print('extreme year', time.asctime((year, 6, 1) + (0,) * 6))
    for year in (2147483648, -2147481749):
        error('year ' + str(year), time.asctime, (year, 6, 1) + (0,) * 6)
finally:
    if saved_tz is None:
        del os.environ['TZ']
    else:
        os.environ['TZ'] = saved_tz
    time.tzset()
