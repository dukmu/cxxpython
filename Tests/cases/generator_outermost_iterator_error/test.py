try:
    g = (x for x in 1)
except TypeError as e:
    print(repr(type(e).__name__))
