from itertools import product, permutations, combinations, combinations_with_replacement

class Index:
    def __index__(self):
        return 2

print(list(product('ab', repeat=Index())))
print(list(combinations('abc', Index())))
print(list(combinations_with_replacement('ab', Index())))
for make in (lambda: permutations('abc', Index()),
             lambda: product('a', repeat=-1),
             lambda: combinations('abc', -1),
             lambda: combinations_with_replacement('abc', -1),
             lambda: permutations('abc', -1),
             lambda: product('a', repeat=None),
             lambda: combinations('abc', 1 << 100),
             lambda: permutations('abc', 1 << 100),
             lambda: product('a', repeat=1 << 100),
             lambda: product('a', repeat=2, unknown=1)):
    try:
        make()
    except Exception as e:
        print(type(e).__name__)

class Broken:
    def __iter__(self):
        print('consume')
        raise ValueError('source')

for make in (lambda: product(Broken()),
             lambda: combinations(Broken(), -1),
             lambda: combinations_with_replacement(Broken(), -1),
             lambda: permutations(Broken(), 'bad')):
    try:
        make()
    except Exception as e:
        print(type(e).__name__, str(e))
print(list(product(Broken(), repeat=0)))
