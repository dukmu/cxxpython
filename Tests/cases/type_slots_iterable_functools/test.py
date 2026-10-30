from functools import cmp_to_key

key = cmp_to_key(lambda left, right: (left > right) - (left < right))
first = key(3)
second = key(1)
print(first.obj, second.obj, first > second, second < first)
print(sorted([3, 1, 2], key=key))
