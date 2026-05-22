x = 4
x += 3
x *= 2
x -= 5
print(x)

s = 'py'
s += 'thon'
print(s)

class Box:
    pass

box = Box()
box.value = 2
hits = 0

def get_box():
    global hits
    hits = hits + 1
    return box

get_box().value += 5
print(box.value)
print(hits)

items = [2]
object_hits = 0
index_hits = 0

def get_items():
    global object_hits
    object_hits = object_hits + 1
    return items

def get_index():
    global index_hits
    index_hits = index_hits + 1
    return 0

get_items()[get_index()] *= 4
print(items)
print(object_hits)
print(index_hits)

mapping = {'x': 2}
mapping['x'] += 5
print(mapping['x'])
