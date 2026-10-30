import gc
import sys


class Pause:
    def __init__(self, value):
        self.value = value

    def __await__(self):
        gc.collect()
        yield self.value
        return self.value


class Items:
    def __init__(self, values):
        self.values = iter(values)

    def __aiter__(self):
        return self

    async def __anext__(self):
        try:
            value = next(self.values)
        except StopIteration:
            raise StopAsyncIteration
        await Pause(value)
        return value


def drive(coro):
    pauses = []
    while True:
        try:
            pauses.append(coro.send(None))
        except StopIteration as done:
            return pauses, done.value


async def consume():
    values = []
    async for item in Items(range(4)):
        values.append(item)
    else:
        values.append("else")
    return values


async def control():
    events = []
    async for item in Items(range(4)):
        try:
            if item == 1:
                continue
            events.append(item)
            if item == 2:
                break
        finally:
            events.append("finally")
    else:
        events.append("wrong else")
    async for item in Items([]):
        events.append("wrong body")
    else:
        events.append("empty else")
    return events


async def nested():
    values = []
    async for a in Items([1, 2]):
        async for b in Items([3, 4]):
            values.append((a, b))
    return values


async def early_return():
    async for item in Items([9, 10]):
        return item


async def exception_state():
    try:
        raise ValueError("outer")
    except ValueError as outer:
        async for item in Items([1]):
            assert sys.exception() is outer
        assert sys.exception() is outer
    return sys.exception() is None


async def body_error():
    async for item in Items([1]):
        raise StopAsyncIteration("body")


async def unpack_error():
    async for a, b in Items([None]):
        pass


print("consume", drive(consume()))
print("control", drive(control()))
print("nested", drive(nested()))
print("return", drive(early_return()))
print("exception state", drive(exception_state()))
for factory in (body_error, unpack_error):
    try:
        drive(factory())
    except Exception as error:
        print(factory.__name__, type(error).__name__, str(error))


async def generated():
    yield 1
    await Pause("generator")
    yield 2


async def consume_generator():
    values = []
    async for item in generated():
        values.append(item)
    return values


print("async generator", drive(consume_generator()))
