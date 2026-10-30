import gc
import sys

events = []


class Pause:
    def __await__(self):
        try:
            yield "pause"
        finally:
            events.append("await closed")


class Items:
    def __aiter__(self):
        return self

    async def __anext__(self):
        try:
            await Pause()
        except ValueError:
            return 12
        finally:
            gc.collect()
            events.append("next finally")
        raise StopAsyncIteration


async def task():
    try:
        async for value in Items():
            events.append(value)
            break
        else:
            events.append("exhausted")
    finally:
        events.append("task finally")
    return "done"


for action in ("send", "throw", "stop", "close"):
    events.clear()
    coro = task()
    print(action, coro.send(None))
    try:
        if action == "send":
            coro.send(None)
        elif action == "throw":
            coro.throw(ValueError("resume"))
        elif action == "stop":
            coro.throw(StopAsyncIteration("stop"))
        else:
            print("closed", coro.close())
    except StopIteration as done:
        print("returned", done.value)
    print("events", events, sys.exception() is None)


events.clear()
coro = task()
coro.send(None)
try:
    coro.throw(object())
except TypeError:
    print("throw rejected", coro.cr_suspended)
try:
    coro.send(None)
except RuntimeError as error:
    print("after rejection", str(error), events)
