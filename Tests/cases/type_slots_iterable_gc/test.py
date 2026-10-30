import gc

events = []
namespace = {}
class Names:
    def __iter__(self):
        del namespace['__slots__']
        gc.collect()
        events.append('iter')
        return self
    def __length_hint__(self):
        gc.collect()
        events.append('hint')
        return 2
    def __next__(self):
        gc.collect()
        if len(events) == 2:
            events.append('first')
            return ''.join(['first', '_slot'])
        if len(events) == 3:
            events.append('second')
            return ''.join(['second', '_slot'])
        events.append('done')
        raise StopIteration

namespace['__slots__'] = Names()
cls = type('CollectedSlots', (), namespace)
gc.collect()
value = cls()
value.first_slot = ['survives', 'collection']
value.second_slot = 23
gc.collect()
print(value.first_slot, value.second_slot, hasattr(value, '__dict__'))
print(events)

def generated_names():
    for i in range(3):
        name = 'generated_' + str(i)
        yield name
        del name
        gc.collect()

source = generated_names()
cls = type('GeneratedSlots', (), {'__slots__': source})
print(list(source))
value = cls()
for i in range(3):
    setattr(value, 'generated_' + str(i), i * 10)
    gc.collect()
print(value.generated_0, value.generated_1, value.generated_2)
