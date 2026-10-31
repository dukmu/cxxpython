import logging
import time

record = logging.LogRecord('format.test', logging.WARNING, 'sample.py', 42,
                           'value=%s', ('abc',), None, 'work')
record.created = 946684800.125
record.msecs = 125.0
for fmt, style in [('%(levelname)s %(message)s', '%'),
                   ('{levelname} {message}', '{'),
                   ('$levelname $message', '$')]:
    formatter = logging.Formatter(fmt, style=style)
    print(formatter.format(record))
formatter = logging.Formatter('%(asctime)s %(message)s')
formatter.converter = time.gmtime
print(formatter.format(record))
formatter.datefmt = '%Y/%m/%d %H:%M:%S'
print(formatter.format(record))
print(logging.Formatter('{missing} {message}', style='{',
                        defaults={'missing': 'default'}).format(record))
for fmt, style in [('{message}', '%'), ('%(message)s', '{'), ('$message', '?')]:
    try:
        logging.Formatter(fmt, style=style)
    except ValueError as exc:
        print(type(exc).__name__, str(exc))
try:
    logging.Formatter('%(missing)s').format(record)
except ValueError as exc:
    print(type(exc).__name__, str(exc))
