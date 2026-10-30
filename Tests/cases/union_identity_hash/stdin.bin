class EqualMeta(type):
    def __eq__(self, other):
        return isinstance(other, EqualMeta)
    def __hash__(self):
        return 123

class A(metaclass=EqualMeta):
    pass
class B(metaclass=EqualMeta):
    pass
u = A | B
print(len(u.__args__), u.__args__[0] is A, u.__args__[1] is B)
print((u | A) is u)
print(u == (B | A), hash(u) == hash(frozenset((A, B))))

class UnhashableMeta(type):
    __hash__ = None

class Unhashable(metaclass=UnhashableMeta):
    pass
bad = Unhashable | int
print(bad.__args__[0] is Unhashable)
for action in (lambda: hash(bad), lambda: bad == (int | Unhashable)):
    try:
        action()
    except TypeError as e:
        print(type(e).__name__)

class ReplacementMeta(type):
    def __or__(self, other):
        print('replacement-or', other)
        return 'custom-result'

class Replacement(metaclass=ReplacementMeta):
    pass

class Parameter:
    def __typing_subst__(self, value):
        return value
    def __repr__(self):
        return 'T'

GenericAlias = type(list[int])
T = Parameter()
class ReplacingAlias(GenericAlias):
    def __getitem__(self, values):
        return Replacement
alias = ReplacingAlias(list, T) | int
print(alias[str])
