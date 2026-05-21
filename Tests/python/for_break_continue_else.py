values = []
for x in range(4):
    if x == 2:
        break
    values.append(x)
else:
    values.append('no-break')
print(values)

values = []
for x in range(3):
    if x == 1:
        continue
    values.append(x)
else:
    values.append('done')
print(values)

values = []
for x in range(2):
    values.append(x)
else:
    values.append('exhausted')
print(values)
