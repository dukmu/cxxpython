import sys


try:
    sys.exit()
except SystemExit as exc:
    print(type(exc))
    print(exc.args)

try:
    sys.exit(5)
except SystemExit as exc:
    print(type(exc))
    print(exc.args)
