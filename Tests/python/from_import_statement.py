from import_target import value
from import_target import value as alias_value

print(value)
print(alias_value)

def local_read():
    from import_target import value as local_value
    return local_value

print(local_read())
