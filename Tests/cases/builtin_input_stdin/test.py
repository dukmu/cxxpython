import sys

print(repr(input('first> ')))
print(repr(sys.stdin.readline()))
print(repr(input()))
try:
    input('last> ')
except EOFError as error:
    print(type(error).__name__, str(error), isinstance(error, Exception))
print(sys.stdin.closed, sys.stdout.closed, sys.stderr.closed)
