import sys

print(sys.exc_info() == (None, None, None))


def current_exception_value():
    return sys.exc_info()[1]


try:
    raise TypeError('outer')
except TypeError as outer:
    info = sys.exc_info()
    print(info[0] is TypeError)
    print(info[1] is outer)
    print(type(info[2]))
    print(info[2].tb_frame.f_code.co_name == '<module>')
    print(info[2].tb_lineno > 0)
    print(current_exception_value() is outer)
    try:
        raise ValueError('inner')
    except ValueError as inner:
        inner_info = sys.exc_info()
        print(inner_info[0] is ValueError)
        print(inner_info[1] is inner)
    restored_info = sys.exc_info()
    print(restored_info[0] is TypeError)
    print(restored_info[1] is outer)

print(sys.exc_info() == (None, None, None))
