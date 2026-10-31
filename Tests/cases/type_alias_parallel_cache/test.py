import _thread
import gc

mutex = _thread.allocate_lock()
entered = [_thread.allocate_lock(), _thread.allocate_lock()]
finished = [_thread.allocate_lock(), _thread.allocate_lock()]
release = _thread.allocate_lock()
for gate in entered + finished + [release]:
    gate.acquire()
calls = []
values = []
errors = []

def evaluate():
    with mutex:
        index = len(calls)
        calls.append('called')
    entered[index].release()
    release.acquire()
    release.release()
    gc.collect()
    return {'answer': 42}

type Shared = evaluate()

def worker(index):
    try:
        values.append(Shared.__value__)
        gc.collect()
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        finished[index].release()

for index in range(2):
    _thread.start_new_thread(worker, (index,))
for gate in entered:
    gate.acquire()
release.release()
for gate in finished:
    gate.acquire()
gc.collect()
cached = Shared.__value__
print(errors, len(calls), [value['answer'] for value in values],
      cached['answer'], any(cached is value for value in values))
