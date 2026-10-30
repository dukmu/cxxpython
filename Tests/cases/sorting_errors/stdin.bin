items = [3, 1, 2]
def mutate(value):
    items.append(99)
    return value
try:
    items.sort(key=mutate)
except ValueError as error:
    print(type(error).__name__, str(error))
print(items)
items = [3, 1, 2]
def fail(value):
    items.append(99)
    raise RuntimeError('key failed')
try:
    items.sort(key=fail)
except RuntimeError as error:
    print(type(error).__name__, str(error))
print(items)
class Bad:
    def __lt__(self, other):
        raise RuntimeError('compare failed')
items = [Bad(), Bad()]
try:
    items.sort()
except RuntimeError as error:
    print(type(error).__name__, str(error))
print(len(items))
