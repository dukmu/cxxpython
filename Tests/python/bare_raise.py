saved = None

try:
    try:
        raise ValueError('same')
    except ValueError as exc:
        saved = exc
        raise
except ValueError as reraised:
    print(saved is reraised)
    print(type(reraised))

try:
    try:
        raise TypeError('bare')
    except:
        raise
except TypeError as exc:
    print(type(exc))

try:
    raise
except RuntimeError as exc:
    print(type(exc))
print(str(exc))
