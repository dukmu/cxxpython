import _thread
import gc

source = 'value = 0\n' + 'value += 1\n' * 400
gates = [_thread.allocate_lock() for _ in range(5)]
results = [None] * 4
failures = []


def compile_worker(index):
    try:
        for iteration in range(4):
            code = compile(source, '<parallel-compiler>', 'exec')
            namespace = {}
            exec(code, namespace)
            assert namespace['value'] == 400
            gc.collect()
        results[index] = namespace['value']
    except BaseException as error:
        failures.append(type(error).__name__)
    finally:
        gates[index].release()


def collector():
    try:
        for iteration in range(16):
            gc.collect()
    except BaseException as error:
        failures.append(type(error).__name__)
    finally:
        gates[4].release()


for gate in gates:
    gate.acquire()
for index in range(4):
    _thread.start_new_thread(compile_worker, (index,))
_thread.start_new_thread(collector, ())
for gate in gates:
    gate.acquire()
print('compiled and executed', results)
print('failures', failures)
