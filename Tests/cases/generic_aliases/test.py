calls = []


def bound():
    calls.append("bound")
    if len(calls) == 1:
        raise ValueError("retry")
    return int


def constraints():
    calls.append("constraints")
    if calls.count("constraints") == 1:
        raise ValueError("retry")
    return int


type Alias[T: bound(), U: (constraints(), str), *Ts, **P] = (T, U, Ts, P)
print("alias", Alias.__name__, Alias.__type_params__)
t, u, ts, p = Alias.__type_params__
print("before", calls)
for parameter, attribute in ((t, "__bound__"), (u, "__constraints__")):
    try:
        getattr(parameter, attribute)
    except ValueError as exc:
        print("failure", type(exc).__name__)
    print("retry", getattr(parameter, attribute))
    print("cached", getattr(parameter, attribute))
print("calls", calls)
print("value", Alias.__value__)
print("identity", Alias.__value__[0] is t, Alias.__value__[2] is ts, Alias.__value__[3] is p)

type Forward[T: Later] = Later
Later = str
print("forward", Forward.__type_params__[0].__bound__, Forward.__value__)


def make_alias():
    value = int
    type Captured[T: value] = value
    value = str
    return Captured


captured = make_alias()
print("late", captured.__type_params__[0].__bound__, captured.__value__)
