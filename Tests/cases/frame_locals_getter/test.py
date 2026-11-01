import sys


def inspect(arg):
    value = 3

    def closure():
        return arg

    frame = sys._getframe()
    snapshot = frame.f_locals
    print("initial", snapshot["arg"], snapshot["value"])
    value = 4
    print("refreshed", frame.f_locals["value"], snapshot is frame.f_locals)
    return frame


finished = inspect(7)
print("finished", finished.f_locals["arg"], finished.f_locals["value"])
print("module", sys._getframe().f_locals is globals())


class Example:
    snapshot = sys._getframe().f_locals
    marker = 12


print("class", Example.snapshot["marker"])
