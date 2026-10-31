import gc
import threading
from datetime import date, datetime, timedelta, timezone

failures = []
results = [None] * 4
base = datetime(2024, 2, 28, 12, tzinfo=timezone.utc)


def worker(index):
    try:
        for step in range(20):
            value = base + timedelta(days=index, minutes=step)
            assert datetime.fromisoformat(value.isoformat()) == value
            assert date.fromordinal(value.date().toordinal()) == value.date()
            gc.collect()
        results[index] = value.isoformat()
    except BaseException as exc:
        failures.append((index, type(exc).__name__, str(exc)))


threads = [threading.Thread(target=worker, args=(index,), daemon=True) for index in range(4)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(30)
assert not any(thread.is_alive() for thread in threads), 'worker timed out'
assert not failures, failures
print('results', results)
print('failures', failures)
