items = []
i = 0
while i < 4:
    i = i + 1
    if i == 2:
        continue
    items.append(i)
else:
    items.append(99)
print(items)

j = 0
while True:
    j = j + 1
    if j == 3:
        break
else:
    j = 100
print(j)

print('left' and 'right')
print('left' or 'right')
print('' or 'fallback')
print('' and 'fallback')

seen = []
def mark(value):
    seen.append(value)
    return value

print(mark('') and mark('no'))
print(seen)
print(mark('yes') or mark('no'))
print(seen)

print('ok' if True and (False or True) else 'bad')
