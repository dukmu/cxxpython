import os
import posix

t = os.terminal_size((80, 24))
print(type(t) is os.terminal_size, os.terminal_size is posix.terminal_size)
print("terminal_size" in os.__all__, "get_terminal_size" in os.__all__)
print(t, t.columns, t.lines, t[0], t[1], len(t), tuple(t))
print(os.terminal_size([3, 4], {}))
print(os.terminal_size.n_fields, os.terminal_size.n_sequence_fields, os.terminal_size.n_unnamed_fields)
try:
    t.columns = 1
except AttributeError:
    print("readonly")
try:
    os.terminal_size((1, 2), None)
except TypeError:
    print("bad dict")
for seq in ((), (1,), (1, 2, 3)):
    try:
        os.terminal_size(seq)
    except TypeError:
        print("bad length")
