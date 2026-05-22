pass

value = 2
if value == 1:
    result = 'one'
elif value == 2:
    result = 'two'
else:
    result = 'other'
print(result)

if True: inline = 'yes'
else: inline = 'no'
print(inline)

items = []
for item in range(3): items.append(item)
print(items)

def choose(flag): return 'left' if flag else 'right'
print(choose(True))
print(choose(False))

class Marker: pass
print(Marker().__class__ is Marker)

a = 1; b = 2; c = 3
print(a + b + c)

print(1 < 2 < 3)
print(1 < 3 < 2)
print(1 < 2 == 2)
print('yes' if 1 < 2 < 3 else 'no')
