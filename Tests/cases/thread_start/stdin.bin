import _thread
import gc

done = _thread.allocate_lock()
done.acquire()
result = []
def work(value, extra=0):
    result.append((value + extra, _thread.get_ident(), _thread.get_native_id()))
    gc.collect()
    done.release()

ident = _thread.start_new_thread(work, (20,), {'extra': 22})
done.acquire()
print(result[0][0], ident == result[0][1], result[0][2] > 0)
done.release()
for function, args, kwargs in [(1, (), {}), (work, [], {}), (work, (), None)]:
    try:
        _thread.start_new_thread(function, args, kwargs)
    except TypeError:
        print('TypeError')
try:
    _thread.exit()
except SystemExit:
    print('SystemExit')
