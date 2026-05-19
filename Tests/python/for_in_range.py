total = 0
for value in range(5):
    total = total + value
print(total)

letters = ''
for ch in "ab":
    letters = letters + ch
print(letters)

items = []
for item in [1, 2, 3]:
    items.append(item)
print(items)

def last_value():
    result = -1
    for n in range(3):
        result = n
    return result

print(last_value())
