import sys

events = []
saved = []


def target():
    value = 0
    value = 9
    value = value + 2
    return value


start = target.__code__.co_firstlineno


def trace(frame, event, arg):
    if frame.f_code is target.__code__ and event == "line":
        events.append(frame.f_lineno - start)
        if frame.f_lineno == start + 2:
            frame.f_lineno = start + 3
            saved.append(frame)
            print("assigned", frame.f_lineno - start)
    return trace


sys.settrace(trace)
print("result", target())
sys.settrace(None)
print("events", events)
try:
    saved[0].f_lineno = start + 1
except ValueError:
    print("outside rejected")


def protected():
    value = 0
    for item in (1,):
        value = item
    return value


protected_start = protected.__code__.co_firstlineno


def reject_entry(frame, event, arg):
    if frame.f_code is protected.__code__ and event == "line":
        if frame.f_lineno == protected_start + 1:
            try:
                frame.f_lineno = protected_start + 3
            except ValueError:
                print("loop entry rejected")
    return reject_entry


sys.settrace(reject_entry)
print("protected", protected())
sys.settrace(None)


class Manager:
    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False


def blocks():
    value = 0
    try:
        raise ValueError()
    except ValueError:
        value = 1
    with Manager():
        value = 2
    return value


blocks_start = blocks.__code__.co_firstlineno


def reject_blocks(frame, event, arg):
    if frame.f_code is blocks.__code__ and event == "line":
        if frame.f_lineno == blocks_start + 1:
            for offset in (5, 7):
                try:
                    frame.f_lineno = blocks_start + offset
                except ValueError:
                    print("block entry rejected", offset)
    return reject_blocks


sys.settrace(reject_blocks)
print("blocks", blocks())
sys.settrace(None)
