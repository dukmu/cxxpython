from itertools import tee, groupby

holder = []
def recursive():
    try:
        next(holder[1])
    except RuntimeError as exc:
        print(type(exc).__name__, str(exc))
    yield 7

holder.extend(tee(recursive()))
print(next(holder[0]), next(holder[1]))

class Key:
    def __init__(self, value):
        self.value = value
    def __eq__(self, other):
        print('compare', self.value, other.value)
        return self.value == other.value

for key, values in groupby([1, 1, 2], key=Key):
    print('group', key.value, list(values))
