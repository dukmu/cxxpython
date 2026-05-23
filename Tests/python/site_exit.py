import builtins
import site


print(dir(builtins).__contains__('SystemExit'))
print(dir(builtins).__contains__('exit'))
print(dir(builtins).__contains__('quit'))
print(type(exit))
print(repr(exit).__contains__('exit()'))
print(repr(quit).__contains__('quit()'))

try:
    exit(4)
except SystemExit as exc:
    print(type(exc))
    print(exc.args)

try:
    quit()
except SystemExit as exc:
    print(type(exc))
    print(exc.args)
