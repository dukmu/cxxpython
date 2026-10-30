import gc

gc.disable()
target = []
source = [target, target]
mapping = {'value': target}
objects = gc.get_objects()
print(any(obj is target for obj in objects))
print(any(obj is source for obj in objects))
del objects
refs = gc.get_referents(source)
print(sum(obj is target for obj in refs))
refs = gc.get_referents(mapping)
print(any(obj is target for obj in refs))
refs = gc.get_referrers(target)
print(any(obj is source for obj in refs), any(obj is mapping for obj in refs))
print(gc.get_referents(), gc.get_referrers())
print(gc.is_tracked(source), gc.is_finalized(source))
source.append(source)
gc.collect()
print(source[0] is target, source[-1] is source)
print(len(gc.get_stats()), len(gc.get_count()))
before = gc.get_stats()
gc.collect()
after = gc.get_stats()
print(sum(s['collections'] for s in after) > sum(s['collections'] for s in before))
for value in [-2, 3, 100000000000000000000000]:
    try:
        gc.get_objects(value)
    except (ValueError, OverflowError):
        print('bad generation')
gc.enable()
