import gc

calls = []
class Custom:
    def __round__(self, *args):
        gc.collect()
        calls.append(args)
        return ('custom', args)

class Index:
    def __index__(self):
        gc.collect()
        return -1

class Integer(int):
    pass

class Floating(float):
    pass

value = Custom()
print('custom', round(value), round(value, None), round(value, 3))
print('calls', calls)
print('keywords', round(number=125, ndigits=Index()))
print('index', round(125, Index()), round(12.5, Index()))
print('subclasses', type(round(Integer(12))).__name__, type(round(Floating(1.25), 1)).__name__)
print('none', round(12, None), round(1.25, None), float.__round__(1.25, None))
try:
    int.__round__(12, None)
except TypeError:
    print('int direct none rejected')
for obj in (object(), 'a'):
    try:
        round(obj)
    except TypeError:
        print('missing round rejected')
try:
    round(2.0, 1.0)
except TypeError:
    print('invalid digits rejected')
