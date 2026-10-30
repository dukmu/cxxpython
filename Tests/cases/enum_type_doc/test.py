class Empty:
    pass


class Documented:
    """Explicit documentation."""


class Child(Documented):
    pass


Dynamic = type('Dynamic', (), {})
print(Empty.__doc__, Empty().__doc__, Empty.__dict__['__doc__'])
print(Documented.__doc__, Documented().__doc__)
print(Child.__doc__, Child().__doc__, Dynamic.__doc__)
