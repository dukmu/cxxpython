import time
from datetime import date, datetime

value = time.strptime('2024-02-29 23:08:07', '%Y-%m-%d %H:%M:%S')
print('fields', tuple(value), value.tm_year, value.tm_yday)
print('format', time.strftime('%Y-%m-%d %H:%M:%S', value))
print('datetime', datetime.strptime('2024-060 11:59 PM', '%Y-%j %I:%M %p'))
print('iso week', datetime.strptime('2020-W53-7', '%G-W%V-%u').date())
print('offset', datetime.strptime('2024-03-01 +0530', '%Y-%m-%d %z').isoformat())
print('microseconds', datetime.strptime('12:34:56.1234', '%H:%M:%S.%f').time())
for text, fmt in [('2023-02-29', '%Y-%m-%d'), ('2024 tail', '%Y'), ('2024', '%Q')]:
    try:
        time.strptime(text, fmt)
    except ValueError as exc:
        print('error', type(exc).__name__)
    else:
        raise AssertionError(text)
print('date', date(2024, 2, 29).strftime('%Y-%m-%d'))
