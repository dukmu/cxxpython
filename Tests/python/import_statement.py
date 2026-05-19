import import_target
import import_target as target_alias

print(import_target.value)
print(import_target is target_alias)

def load_again():
    import import_target as local_target
    return local_target.value

print(load_again())
