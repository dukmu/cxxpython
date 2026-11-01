from contextvars import Context, ContextVar, Token

var = ContextVar('var')
other = ContextVar('other')
ctx = Context()


def error(call):
    try:
        call()
    except Exception as exc:
        print(type(exc).__name__)


error(var.get)
print(var.get(None))
token = var.set('outer')
print(token.var is var, token.old_value is Token.MISSING)
second = var.set('inner')
print(second.old_value)
error(lambda: other.reset(second))
error(lambda: ctx.run(var.reset, second))
print(var.get())
var.reset(second)
print(var.get())
error(lambda: var.reset(second))


def nested():
    print(var.get('unset'))
    var.set('inside')
    error(lambda: ctx.run(lambda: None))
    raise ValueError('failure')


error(lambda: ctx.run(nested))
print(var.get(), ctx[var])
var.reset(token)
print(var.get('unset'))
var.set(Token.MISSING)
marker_token = var.set('temporary')
var.reset(marker_token)
print(var.get() is Token.MISSING)
