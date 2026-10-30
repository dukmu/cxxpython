from itertools import product, permutations, combinations, combinations_with_replacement

for n in range(5):
    values = range(n)
    print([len(list(product(values, repeat=r))) for r in range(4)])
    print([len(list(permutations(values, r))) for r in range(5)])
    print([len(list(combinations(values, r))) for r in range(5)])
    print([len(list(combinations_with_replacement(values, r))) for r in range(5)])
