def show(label, thunk):
    try:
        thunk()
    except TypeError as exc:
        print(label, exc)


def bad_sum():
    return sum()


def bad_format():
    return format()


def bad_eval():
    return eval()


def bad_exec():
    return exec()


def bad_import():
    return __import__()


def bad_range():
    return range()


def bad_enumerate():
    return enumerate()


show('sum', bad_sum)
show('format', bad_format)
show('eval', bad_eval)
show('exec', bad_exec)
show('import', bad_import)
show('range', bad_range)
show('enumerate', bad_enumerate)
