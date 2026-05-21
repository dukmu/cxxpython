events = []
for x in range(3):
    try:
        if x == 1:
            continue
        if x == 2:
            break
        events.append(x)
    finally:
        events.append('f' + str(x))
print(events)
