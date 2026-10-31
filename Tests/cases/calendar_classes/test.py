import calendar
from datetime import date

print('leaps', calendar.isleap(2000), calendar.isleap(1900), calendar.leapdays(1996, 2025))
print('month', calendar.monthrange(2024, 2), calendar.weekday(2024, 2, 29))
cal = calendar.Calendar(firstweekday=6)
print('weekdays', list(cal.iterweekdays()))
print('monthdays', cal.monthdayscalendar(2024, 2))
print('day tuples', list(cal.itermonthdays3(2024, 2))[:3], list(cal.itermonthdays4(2024, 2))[-3:])
print('dates', list(cal.itermonthdates(2024, 2))[0], list(cal.itermonthdates(2024, 2))[-1])
print('text', calendar.TextCalendar().formatmonth(2024, 2).splitlines()[:3])
html = calendar.HTMLCalendar().formatmonth(2024, 2)
print('html', '<table' in html, 'February 2024' in html, '29' in html)
print('epoch', calendar.timegm((1970, 1, 1, 0, 0, 0)), date(2024, 2, 29).weekday())
try:
    calendar.monthrange(2024, 13)
except calendar.IllegalMonthError as exc:
    print('error', type(exc).__name__, exc.month)
