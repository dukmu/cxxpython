events = []
class C:
    def __enter__(self): return self
    def __exit__(self, a, b, c): events.append(3)
def f():
    with C():
        try:
            raise ValueError()
        except ValueError:
            yield 1
        finally:
            events.append(2)
g = f()
next(g)
g.close()
print(repr(events))
