import os
import posix


print(os.__all__.__contains__('system'))
print(os.__all__.__contains__('execv'))
print(os.__all__.__contains__('execve'))
print(os.__all__.__contains__('execvp'))
print(os.__all__.__contains__('execvpe'))
print(os.__all__.__contains__('spawnv'))
print(os.__all__.__contains__('spawnve'))
print(os.__all__.__contains__('spawnvp'))
print(os.P_WAIT == posix.P_WAIT)
print(os.P_NOWAIT == posix.P_NOWAIT)

status = os.system('exit 5')
print(os.WIFEXITED(status))
print(os.WEXITSTATUS(status) == 5)

pid = os.fork()
if pid == 0:
    os.execv('/bin/sh', ('sh', '-c', 'exit 9'))
    os._exit(127)
waited = os.waitpid(pid, 0)
print(os.WIFEXITED(waited[1]))
print(os.WEXITSTATUS(waited[1]) == 9)

pid = os.fork()
if pid == 0:
    os.execve(
        '/bin/sh',
        ('sh', '-c', 'exit $CXXPYTHON_STATUS'),
        {'CXXPYTHON_STATUS': '8', 'PATH': '/bin:/usr/bin'},
    )
    os._exit(127)
waited = os.waitpid(pid, 0)
print(os.WIFEXITED(waited[1]))
print(os.WEXITSTATUS(waited[1]) == 8)

pid = os.fork()
if pid == 0:
    os.execvp('sh', ('sh', '-c', 'exit 7'))
    os._exit(127)
waited = os.waitpid(pid, 0)
print(os.WIFEXITED(waited[1]))
print(os.WEXITSTATUS(waited[1]) == 7)

pid = os.fork()
if pid == 0:
    os.execvpe(
        'sh',
        ('sh', '-c', 'exit $CXXPYTHON_STATUS'),
        {'CXXPYTHON_STATUS': '6', 'PATH': '/bin:/usr/bin'},
    )
    os._exit(127)
waited = os.waitpid(pid, 0)
print(os.WIFEXITED(waited[1]))
print(os.WEXITSTATUS(waited[1]) == 6)

print(os.spawnv(os.P_WAIT, '/bin/sh', ('sh', '-c', 'exit 4')) == 4)
print(os.spawnve(
    os.P_WAIT,
    '/bin/sh',
    ('sh', '-c', 'exit $CXXPYTHON_STATUS'),
    {'CXXPYTHON_STATUS': '3', 'PATH': '/bin:/usr/bin'},
) == 3)
print(os.spawnvp(os.P_WAIT, 'sh', ('sh', '-c', 'exit 2')) == 2)
print(os.spawnvpe(
    os.P_WAIT,
    'sh',
    ('sh', '-c', 'exit $CXXPYTHON_STATUS'),
    {'CXXPYTHON_STATUS': '1', 'PATH': '/bin:/usr/bin'},
) == 1)

pid = os.spawnv(os.P_NOWAIT, '/bin/sh', ('sh', '-c', 'exit 0'))
waited = os.waitpid(pid, 0)
print(type(pid))
print(pid > 0)
print(os.WIFEXITED(waited[1]))
print(os.WEXITSTATUS(waited[1]) == 0)

try:
    os.spawnv(99, '/bin/sh', ('sh', '-c', 'exit 0'))
except ValueError as exc:
    print(type(exc))

try:
    os.execv('/definitely/not/cxxpython-executable', ('missing',))
except OSError as exc:
    print(type(exc))
