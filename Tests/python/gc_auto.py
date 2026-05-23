import gc

print(gc.isenabled())
print(type(gc.get_threshold()))
print(len(gc.get_threshold()))
print(gc.set_threshold(8))
print(gc.get_threshold()[0])
gc.disable()
print(gc.isenabled())
gc.enable()
print(gc.isenabled())

for index in range(40):
    value = [index, index + 1, index + 2]

count = gc.get_count()[0]
print(type(count))
print(count < 8)

try:
    gc.set_threshold(-1)
except ValueError as exc:
    print(type(exc))
