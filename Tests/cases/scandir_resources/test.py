import gc
import os

os.mkdir('entries')
with open('entries/file', 'w') as stream:
    stream.write('x')
try:
    baseline = len(os.listdir('/proc/self/fd'))
    iterators = [os.scandir('entries') for i in range(40)]
    retained = next(iterators[0])
    for iterator in iterators:
        iterator.close()
        iterator.close()
        assert list(iterator) == []
    print('closed handles', len(os.listdir('/proc/self/fd')) == baseline)
    print('entry survives close', retained.stat().st_size == 1)
    del iterators, iterator
    for i in range(40):
        iterator = os.scandir('entries')
        next(iterator)
    del iterator
    gc.collect()
    print('abandoned handles', len(os.listdir('/proc/self/fd')) == baseline)
    with os.scandir('entries') as iterator:
        list(iterator)
    print('exhausted handles', len(os.listdir('/proc/self/fd')) == baseline)
finally:
    os.unlink('entries/file')
    os.rmdir('entries')
