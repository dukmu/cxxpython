events = []

def evaluate():
    events.append('evaluate')
    return target

type Forward = evaluate()
print(Forward.__name__, Forward.__type_params__, events)
for attempt in range(2):
    try:
        Forward.__value__
    except NameError as error:
        print(type(error).__name__, error.name, len(events))
target = 42
print(Forward.__value__, Forward.__value__, events)
target = 99
print(Forward.__value__)

type Recursive = list[Recursive]
print(Recursive.__value__.__origin__ is list)
print(Recursive.__value__.__args__[0] is Recursive)

def make_alias():
    captured = 'before'
    type Captured = captured
    captured = 'after'
    return Captured

alias = make_alias()
print(alias.__value__, alias.__value__)

def make_module_alias():
    type Local = int
    return Local

make_module_alias.__module__ = 'changed_module'
local = make_module_alias()
print(local.__module__)
make_module_alias.__module__ = 'another_module'
print(local.__module__)
