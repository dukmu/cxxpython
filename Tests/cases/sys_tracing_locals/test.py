import sys


def outer():
    free = 3

    def target(arg):
        local = 1
        def read_arg():
            return arg
        marker = 0
        return local, arg, free, read_arg()

    return target


target = outer()
line = target.__code__.co_firstlineno + 4


def change(frame, event, arg):
    if frame.f_code is target.__code__ and event == "line" and frame.f_lineno == line:
        mapping = frame.f_locals
        exec("local = 10; arg = 20; free = 30", frame.f_globals, mapping)
    return change


sys.settrace(change)
print("changed", target(2))
sys.settrace(None)


def deleted():
    value = 1
    def read():
        return value
    marker = 0
    try:
        return value
    except UnboundLocalError:
        return "unbound"


delete_line = deleted.__code__.co_firstlineno + 4


def remove(frame, event, arg):
    if frame.f_code is deleted.__code__ and event == "line" and frame.f_lineno == delete_line:
        del frame.f_locals["value"]
    return remove


sys.settrace(remove)
print("deleted", deleted())
sys.settrace(None)
