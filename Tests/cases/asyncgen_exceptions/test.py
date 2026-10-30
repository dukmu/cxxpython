async def bad_stop():
    yield 1
    raise StopAsyncIteration('bad')

async def bad_exit():
    try:
        yield 1
    finally:
        yield 2

def run(request):
    try:
        request.send(None)
    except BaseException as error:
        print(type(error).__name__, str(error))
        if error.__cause__ is not None:
            print('cause', type(error.__cause__).__name__, str(error.__cause__))

g = bad_stop()
run(g.__anext__())
run(g.__anext__())
g = bad_exit()
run(g.__anext__())
run(g.aclose())
run(g.aclose())
