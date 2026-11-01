import gc
from contextvars import Context, ContextVar, copy_context

first = ContextVar('first')
second = ContextVar('second')
ctx = Context()
copies = []


def callback(phase, info):
    if phase == 'start':
        second.set('callback')
        copies.append(copy_context())


def exercise():
    first.set('before')
    gc.callbacks.append(callback)
    try:
        gc.collect()
    finally:
        gc.callbacks.remove(callback)
    token = first.set('after')
    print(first.get(), second.get(), copies[0][first], copies[0][second])
    first.reset(token)
    print(first.get(), second.get(), ctx == copies[0])


ctx.run(exercise)
print(first.get('outside'), second.get('outside'))
