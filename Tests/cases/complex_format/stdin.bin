for value in (3+4j, -0.0-2j, complex(-0.0, -0.0), 2j, complex(1234.5, -6789.25)):
    for spec in ('', '.2f', '+.1e', '.3g', '.3', '^24', '*^24.2f', ',.1f', 'z.1f'):
        print(repr(value), repr(spec), repr(format(value, spec)))
for spec in ('010f', '=20', '%', 'd'):
    try:
        format(1j, spec)
    except ValueError:
        print('invalid', spec)
print(type((-1.0) ** 0.5).__name__, abs((-1.0) ** 0.5 - 1j) < 1e-12)
print(f'{3+4j:.1f}', f'{2j:>8}')
