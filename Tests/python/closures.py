def outer():
    x = 'closed'
    def inner():
        return x
    return inner


f = outer()
print(f())


def outer2():
    x = 'before'
    def inner():
        return x
    x = 'after'
    return inner


print(outer2()())


def outer_arg(value):
    def inner():
        return value
    return inner


print(outer_arg('argument')())


def outer3():
    x = 'deep'
    def middle():
        def inner():
            return x
        return inner
    return middle


print(outer3()()())
