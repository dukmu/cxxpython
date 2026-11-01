import gc


events = []


class Key:
    def __init__(self, name, code):
        self.name = name
        self.code = code
        self.fail = False
        self.action = None

    def __hash__(self):
        events.append('hash:' + self.name)
        return self.code

    def __eq__(self, other):
        events.append('eq:' + self.name)
        if self.fail:
            raise ValueError('comparison')
        if self.action is not None:
            action = self.action
            self.action = None
            action()
            gc.collect()
        return False


def operate(d, key, op):
    if op == 'get':
        return d.get(key)
    if op == 'item':
        return d[key]
    if op == 'store':
        d[key] = 30
        return d.get(key)
    if op == 'setdefault':
        return d.setdefault(key, 30)
    if op == 'erase':
        del d[key]
        return len(d)
    if op == 'pop':
        return d.pop(key)
    return key in d


for named in (False, True):
    for op in ('get', 'item', 'store', 'setdefault', 'erase', 'pop', 'contains'):
        for fail in (False, True):
            target = 'collision-name' if named else Key('target', 19)
            code = hash(target) if named else 19
            first = Key('first', code)
            d = {first: 10, target: 20}
            first.fail = fail
            events.clear()
            try:
                result = operate(d, target, op)
            except ValueError:
                result = 'raised'
            print(named, op, fail, result, events)


solo = Key('solo', 23)
d = {solo: 1}
solo.fail = True
events.clear()
print('identity', d[solo], solo in d, d.setdefault(solo, 2), events)


for action_name in ('clear', 'delete', 'reinsert'):
    first = Key('first', 31)
    target = Key('target', 31)
    d = {first: 1, target: 2}

    def mutate():
        if action_name == 'clear':
            d.clear()
        elif action_name == 'delete':
            del d[first]
        else:
            del d[first]
            d[first] = 3

    first.action = mutate
    events.clear()
    result = d.get(target)
    # Probe order after structural mutation depends on the table representation.
    print('reentry', action_name, result, len(d), events[:2])


first = Key('first', 37)
removed = Key('removed', 37)
query = Key('query', 37)
d = {first: 1, removed: 2}
removed.fail = True
first.action = d.clear
events.clear()
try:
    result = d.get(query, 'missing')
except ValueError:
    result = 'stale candidate'
print('removed candidate', result, len(d), events)


class GrowingKey:
    def __hash__(self):
        return 41

    def __eq__(self, other):
        attempts.append(len(d))
        if len(attempts) > 4:
            raise RuntimeError('unrelated insertion caused repeated comparison')
        d[1000 + len(attempts)] = None
        return False


stored = GrowingKey()
d = {stored: 1}
attempts = []
try:
    result = d.get(GrowingKey(), 'missing')
except RuntimeError:
    result = 'repeated unrelated comparison'
print('unrelated mutation', result, 1 <= len(attempts) <= 4)
