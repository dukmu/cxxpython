value = 'module'


def set_global():
    global value
    value = 'changed'


set_global()
print(value)


def outer():
    item = 'outer'

    def changer():
        nonlocal item
        print(item)
        item = 'inner'

    changer()
    return item


print(outer())


def pass_through():
    item = 'pass'

    def middle():
        nonlocal item
        item = 'through'

    def reader():
        return item

    middle()
    return reader()


print(pass_through())
