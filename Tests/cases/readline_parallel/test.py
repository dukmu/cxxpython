import _thread
import gc
import readline

readline.clear_history()
done = [_thread.allocate_lock(), _thread.allocate_lock()]
failures = []
for lock in done:
    lock.acquire()


def worker(number):
    try:
        for index in range(40):
            readline.add_history(str(number) + ":" + str(index))
            if index % 8 == 0:
                gc.collect()
    except BaseException as error:
        failures.append(type(error).__name__)
    finally:
        done[number].release()


for number in range(2):
    _thread.start_new_thread(worker, (number,))
for lock in done:
    lock.acquire()
assert not failures, failures
assert readline.get_current_history_length() == 80
values = [readline.get_history_item(index) for index in range(1, 81)]
assert sorted(values) == sorted(str(n) + ":" + str(i) for n in range(2) for i in range(40))
readline.clear_history()
print("shared history ok")
