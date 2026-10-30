import gc


class Pause:
    def __init__(self, value):
        self.value = value

    def __await__(self):
        gc.collect()
        yield self.value
        return self.value


events = []


class Items:
    def __init__(self, values):
        self.values = iter(values)

    def __aiter__(self):
        events.append("iter")
        return self

    async def __anext__(self):
        try:
            return next(self.values)
        except StopIteration:
            raise StopAsyncIteration


def drive(coro):
    pauses = []
    while True:
        try:
            pauses.append(coro.send(None))
        except StopIteration as done:
            return pauses, done.value


async def eager():
    a = [x * 2 async for x in Items(range(4)) if x % 2]
    b = {x: await Pause(x + 10) async for x in Items(range(3))}
    c = {await Pause(x) async for x in Items([2, 2, 1])}
    d = [await Pause(x * 3) for x in range(3) if await Pause(x != 1)]
    e = [[y async for y in Items([x, x + 1])] for x in range(2)]
    f = [(x, y) for x in [1, 2] async for y in Items([3, 4])]
    return a, sorted(b.items()), sorted(c), d, e, f


print("eager", drive(eager()))
events.clear()
lazy = (x * 2 async for x in Items([1, 2]))
print("created", events)


async def take(iterator):
    return [x async for x in iterator]


print("module generator", drive(take(lazy)))


def ordinary_function():
    return (await Pause(x) for x in [5, 6])


print("await generator", drive(take(ordinary_function())))


async def nested_generators():
    values = ([y async for y in Items([x])] for x in range(2))
    return [x async for x in values]


print("nested generator", drive(nested_generators()))


async def bindings():
    value = -1
    result = [(value := x) async for x in Items(range(3))]
    funcs = [lambda: x async for x in Items(range(3))]
    return result, value, [fn() for fn in funcs]


print("bindings", drive(bindings()))


async def class_scope():
    class C:
        generator = (x async for x in Items([7]))
    return [x async for x in C.generator]


print("class generator", drive(class_scope()))
for source in (
    "async for x in y: pass",
    "def f():\n async for x in y: pass",
    "[x async for x in y]",
    "def f():\n return [x async for x in y]",
    "async def f():\n class C:\n  values = [x async for x in y]",
):
    try:
        compile(source, "<async scope>", "exec")
    except SyntaxError:
        print("invalid scope")
    else:
        raise AssertionError(source)
