class Pause:
    def __init__(self, label, value=None, fail=False):
        self.label = label
        self.value = value
        self.fail = fail

    def __await__(self):
        yield self.label
        if self.fail:
            raise ValueError(self.label)
        return self.value


class Manager:
    def __init__(self, name, value=42, suppress=False, fail=False):
        self.name = name
        self.value = value
        self.suppress = suppress
        self.fail = fail

    async def __aenter__(self):
        print('enter', self.name)
        return await Pause('enter-' + self.name, self.value)

    async def __aexit__(self, typ, value, tb):
        print('exit', self.name, typ is None, value is None, tb is None)
        return await Pause('exit-' + self.name, self.suppress, self.fail)


def drive(coro):
    try:
        while True:
            print('pause', coro.send(None))
    except StopIteration as result:
        print('result', result.value)
    except Exception as error:
        print('error', type(error).__name__, str(error))


async def normal():
    async with Manager('a') as x, Manager('b') as y:
        print('body', x, y)
        return x + y


async def suppressed():
    async with Manager('s', suppress=True):
        raise ValueError('body')
    return 'suppressed'


async def binding():
    async with Manager('binding', value=1) as (x, y):
        print('unreachable')


async def exit_failure():
    async with Manager('outer'), Manager('inner', fail=True):
        return 'lost'


class FailingEnter:
    async def __aenter__(self):
        return await Pause('failed-enter', fail=True)

    async def __aexit__(self, typ, value, tb):
        print('unexpected exit')


async def enter_failure():
    async with Manager('parent'), FailingEnter():
        print('unreachable')


async def loops():
    for i in (0, 1):
        async with Manager('loop'):
            if i == 0:
                continue
            break
    return 'looped'


drive(normal())
drive(suppressed())
drive(binding())
drive(exit_failure())
drive(enter_failure())
drive(loops())
