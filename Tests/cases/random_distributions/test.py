import random

generator = random.Random(2024)
for name, args in [('uniform', (-4, 9)), ('triangular', (-2, 5, 1)),
                   ('normalvariate', (2, 3)), ('gauss', (-1, 2)),
                   ('lognormvariate', (0, 0.5)), ('expovariate', (0.7,)),
                   ('vonmisesvariate', (1, 2)), ('gammavariate', (2, 3)),
                   ('betavariate', (2, 5)), ('paretovariate', (3,)),
                   ('weibullvariate', (2, 3)), ('binomialvariate', (100, 0.4))]:
    print(name, getattr(generator, name)(*args))
print('boundary', generator.binomialvariate(0, 0.5),
      generator.binomialvariate(10, 0), generator.binomialvariate(10, 1))


class Fixed(random.Random):
    def random(self):
        return 0.25


fixed = Fixed(1)
print('override', fixed.choice('abcd'), fixed.randrange(8), fixed.uniform(0, 4))
