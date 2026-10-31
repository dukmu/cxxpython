from datetime import date, datetime, time, timedelta, timezone


def check(label, func, *args):
    try:
        func(*args)
    except (TypeError, ValueError, OverflowError, ZeroDivisionError) as exc:
        print(label, type(exc).__name__)
    else:
        raise AssertionError(label)


check('date', date, 2023, 2, 29)
check('time', time, 24)
check('fold', lambda: datetime(2024, 1, 1, fold=2))
check('iso date', date.fromisoformat, '2023-02-29')
check('iso time', time.fromisoformat, '25:00')
check('offset', timezone, timedelta(hours=24))
check('overflow', date.max.__add__, timedelta(days=1))
check('divide', timedelta(days=1).__truediv__, 0)
check('naive aware', datetime(2024, 1, 1).__lt__, datetime(2024, 1, 1, tzinfo=timezone.utc))
print('protocol', date(2024, 1, 2).timetuple()[:3], repr(timedelta(seconds=1)))
print('identity', datetime.min.__class__ is datetime, date.min.__class__ is date)
