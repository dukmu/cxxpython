import gc
items = [(2, 'a'), (1, 'b'), (2, 'c'), (1, 'd')]
seen = []
def key(item):
    gc.collect()
    seen.append(item[1])
    print('key', item[1], len(items))
    return item[0]
print(items.sort(key=key))
print(items)
print(seen)
items.sort(key=key, reverse=True)
print(items)
numbers = [5, 1, 3, 1, 2]
numbers.sort()
print(numbers)
numbers.sort(reverse=True)
print(numbers)
