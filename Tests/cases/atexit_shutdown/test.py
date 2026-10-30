import atexit

def finish(value):
    print('exit', value)

atexit.register(finish, 'first')
atexit.register(finish, 'second')
print('main')
