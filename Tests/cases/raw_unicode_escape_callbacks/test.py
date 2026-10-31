import codecs
import gc
import threading

events = []


def replacement(error):
    gc.collect()
    events.append((error.encoding, error.start, error.end, error.reason))
    return '<\u20ac>', error.end


def new_object(error):
    gc.collect()
    error.object = b'XYZ'
    return '!', -1


def incomplete_object(error):
    gc.collect()
    error.object = br'XY\u12'
    return '!', 2


codecs.register_error('raw_collect', replacement)
codecs.register_error('raw_new_object', new_object)
codecs.register_error('raw_incomplete_object', incomplete_object)
print('replacement', codecs.raw_unicode_escape_decode(br'A\u12Ztail', 'raw_collect'))
print('events', events)
print('object and offset', codecs.raw_unicode_escape_decode(br'A\uZ', 'raw_new_object'))
print('nonfinal object', codecs.raw_unicode_escape_decode(br'A\uZ', 'raw_new_object', False))
print('incomplete object', codecs.raw_unicode_escape_decode(br'A\uZ', 'raw_incomplete_object', False))
for label, result, error_type in (
    ('replacement type', (b'bytes', 2), TypeError),
    ('position type', ('?', '2'), TypeError),
    ('position bounds', ('?', 100), IndexError),
    ('negative bounds', ('?', -100), IndexError),
    ('tuple shape', ('?',), TypeError),
):
    def invalid(error, result=result):
        gc.collect()
        return result
    codecs.register_error('raw_invalid', invalid)
    try:
        codecs.raw_unicode_escape_decode(br'\uZ', 'raw_invalid')
    except error_type as exc:
        print(label, type(exc).__name__)
    else:
        raise AssertionError(label)

failures = []
results = [False] * 3


def worker(index):
    try:
        for step in range(8):
            value = codecs.raw_unicode_escape_decode(br'A\uZ', 'raw_new_object')
            assert value == ('A!Z', 4)
            gc.collect()
        results[index] = True
    except BaseException as exc:
        failures.append((index, type(exc).__name__, str(exc)))


threads = [threading.Thread(target=worker, args=(i,), daemon=True) for i in range(3)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(60)
assert not any(thread.is_alive() for thread in threads), 'worker timed out'
assert not failures, failures
print('collecting workers', results, failures)
