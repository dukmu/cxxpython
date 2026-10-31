import logging
import unittest

case = unittest.TestCase()
logger = logging.getLogger('assertlogs.child')
with case.assertLogs('assertlogs', level='INFO') as captured:
    logger.debug('hidden')
    logger.info('hello %s', 'world')
    logger.warning('warning')
print(captured.output)
print([record.name for record in captured.records])
with case.assertNoLogs('assertlogs', level='WARNING'):
    logger.info('allowed')
try:
    with case.assertLogs('assertlogs', level='ERROR'):
        logger.warning('too low')
except AssertionError as exc:
    print('assertLogs:', exc)
try:
    with case.assertNoLogs('assertlogs', level='WARNING'):
        logger.error('not allowed')
except AssertionError as exc:
    print('assertNoLogs:', exc)
