import _thread


print(type(_thread))
print(_thread.__doc__.__contains__('OS threads'))
print(type(_thread.allocate_lock()))
print(_thread.LockType)
print(type(_thread.get_ident()))

lock = _thread.allocate_lock()
print(lock.locked())
print(lock.acquire(False))
print(lock.locked())
print(lock.acquire(False))
print(lock.release())
print(lock.locked())

done = _thread.allocate_lock()
done.acquire()
values = []
main_ident = _thread.get_ident()


def worker(left, right, flag=None):
    values.append(left + right)
    values.append(flag)
    values.append(_thread.get_ident() != main_ident)
    done.release()


ident = _thread.start_new_thread(worker, (2, 3), {'flag': 'kw'})
print(type(ident))
done.acquire()
done.release()
print(values)
