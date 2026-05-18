value = 'global'


def local_value(flag):
    if flag:
        value = 'local'
    return value


print(local_value(False))
