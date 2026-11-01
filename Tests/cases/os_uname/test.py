import os

value = os.uname()
print("type", type(value) is os.uname_result, type(value).__module__, type(value).__name__)
print("shape", len(value), value.n_fields, value.n_sequence_fields, value.n_unnamed_fields)
print("fields", tuple(value) == (value.sysname, value.nodename, value.release,
                                 value.version, value.machine))
print("strings", all(isinstance(field, str) for field in value))
print("repeat", os.uname() == value)
try:
    value.sysname = "other"
except AttributeError:
    print("readonly")
example = os.uname_result((1, 2, 3, 4, 5))
print("constructor", example, example[2], example.machine)
try:
    os.uname_result((1, 2))
except TypeError as error:
    print("bad sequence", str(error))
