import gc
import io
import logging
import os
import threading

logger = logging.getLogger('handlers')
logger.setLevel(logging.INFO)
logger.propagate = False
stream = io.StringIO()
handler = logging.StreamHandler(stream)
handler.setFormatter(logging.Formatter('%(message)s'))
logger.addHandler(handler)

class CollectingFilter:
    def filter(self, record):
        gc.collect()
        return True

handler.addFilter(CollectingFilter())
errors = []
completed = [False] * 4
def worker(index):
    try:
        for item in range(20):
            logger.info('%d:%d', index, item)
    except BaseException as exc:
        errors.append((index, type(exc).__name__, str(exc)))
    finally:
        completed[index] = True
threads = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(5)
assert not any(thread.is_alive() for thread in threads)
assert not errors, errors
assert all(completed), completed
lines = stream.getvalue().splitlines()
print(len(lines), len(set(lines)), sorted(lines)[0], sorted(lines)[-1])
replacement = io.StringIO()
print(handler.setStream(replacement) is stream)
logger.warning('replacement')
print(replacement.getvalue(), end='')
logger.removeHandler(handler)
handler.close()

class BrokenStream:
    def write(self, text):
        raise OSError('write failed')
    def flush(self):
        pass

class ErrorHandler(logging.StreamHandler):
    def handleError(self, record):
        print('handleError', record.getMessage())

broken = ErrorHandler(BrokenStream())
logger.addHandler(broken)
logger.error('problem')
logger.removeHandler(broken)
broken.close()

path = os.path.join('/tmp', 'cxxpython-logging-%s.log' % os.getpid())
try:
    file_handler = logging.FileHandler(path, encoding='utf-8', delay=True)
    print(file_handler.stream is None, os.path.exists(path))
    file_handler.setFormatter(logging.Formatter('%(levelname)s:%(message)s'))
    logger.addHandler(file_handler)
    logger.info('file message')
    logger.removeHandler(file_handler)
    file_handler.close()
    with open(path, encoding='utf-8') as file:
        print(file.read(), end='')
finally:
    if os.path.exists(path):
        os.unlink(path)
