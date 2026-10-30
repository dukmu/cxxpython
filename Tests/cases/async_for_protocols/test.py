import gc


async def consume(value):
    async for item in value:
        pass


def check(label, value):
    coro = consume(value)
    try:
        coro.send(None)
    except StopIteration:
        print(label, "done")
    except Exception as error:
        print(label, type(error).__name__, str(error))
        cause = error.__cause__
        if cause is not None:
            print("cause", type(cause).__name__, cause is error.__context__)
    else:
        raise AssertionError("unexpected suspension")


class BadIter:
    def __aiter__(self):
        return []


class Returns:
    def __init__(self, value):
        self.value = value

    def __aiter__(self):
        return self

    def __anext__(self):
        gc.collect()
        return self.value


class BadAwait:
    def __await__(self):
        return 1


class FailingAwait:
    def __await__(self):
        raise LookupError("await lookup")


class Done:
    def __aiter__(self):
        return self

    def __anext__(self):
        raise StopAsyncIteration


class Broken(Done):
    def __anext__(self):
        raise ValueError("next body")


check("no iter", 1)
check("bad iter", BadIter())
check("bad next", Returns(3))
check("bad await", Returns(BadAwait()))
check("failing await", Returns(FailingAwait()))
check("done", Done())
check("next error", Broken())


class Deleted:
    def __aiter__(self):
        return self

    def __anext__(self):
        raise AssertionError


class RemoveNext:
    def __await__(self):
        del Deleted.__anext__
        yield "remove"
        return 1


Deleted.__anext__ = lambda self: RemoveNext()
pending = consume(Deleted())
print("suspended", pending.send(None))
try:
    pending.send(None)
except TypeError as error:
    print("deleted next", str(error))


class InstanceOnly:
    pass


item = InstanceOnly()
item.__aiter__ = lambda: Done()
check("instance ignored", item)


class Target:
    def __setattr__(self, name, value):
        raise StopAsyncIteration("target")


class OneAwait:
    def __await__(self):
        if False:
            yield
        return 1


async def target_error():
    async for Target().value in Returns(OneAwait()):
        pass


try:
    target_error().send(None)
except StopAsyncIteration as error:
    print("target not swallowed", str(error))
