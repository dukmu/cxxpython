from contextvars import Context, ContextVar, copy_context

var = ContextVar('var', default='default')
other = ContextVar('other')
ctx = Context()
print(var.name, var.get(), var.get('override'))
print(len(ctx), ctx.get(var), ctx.get(var, 'absent'), var in ctx)


def exercise():
    first = var.set('first')
    other.set('second')
    saved = copy_context()
    duplicate = ctx.copy()
    var.set('changed')
    print(var.get(), ctx[var], saved[var], duplicate[var])
    print(len(saved), var in saved, other in saved, saved[other])
    print(set(saved.keys()) == {var, other})
    print(set(saved.values()) == {'first', 'second'})
    print(set(saved.items()) == {(var, 'first'), (other, 'second')})
    duplicate.run(var.set, 'duplicate')
    print(var.get(), duplicate[var], saved[var])
    var.reset(first)
    print(var.get(), var in ctx, var in saved)


ctx.run(exercise)
print(var.get(), ctx.get(other), len(copy_context()))
