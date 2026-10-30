from itertools import accumulate, batched

print(list(accumulate([1, 2, 3, 4])))
print(list(accumulate([1, 2, 3], initial=10)))
print(list(accumulate([], initial=0)))
print(list(accumulate([2, 3, 4], lambda x, y: x * y)))
print(list(batched('ABCDEFG', 3)))
print(list(batched(iterable=[1, 2, 3], n=2)))
try:
    batched([], 0)
except ValueError as e:
    print(str(e))
