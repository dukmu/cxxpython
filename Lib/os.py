"""Expose portable operating-system interfaces on top of the POSIX substrate."""

import posix
import posixpath as path

__all__ = [
    'name',
    'sep',
    'linesep',
    'curdir',
    'pardir',
    'pathsep',
    'path',
    'environ',
    'WEXITSTATUS',
    'WIFEXITED',
    'WNOHANG',
    'O_APPEND',
    'O_CREAT',
    'O_RDONLY',
    'O_RDWR',
    'O_TRUNC',
    'O_WRONLY',
    'P_NOWAIT',
    'P_WAIT',
    '_exit',
    'close',
    'dup',
    'dup2',
    'execl',
    'execle',
    'execlp',
    'execlpe',
    'execv',
    'execve',
    'execvp',
    'execvpe',
    'fork',
    'getcwd',
    'getpid',
    'kill',
    'listdir',
    'open',
    'pipe',
    'read',
    'spawnl',
    'spawnle',
    'spawnlp',
    'spawnlpe',
    'spawnv',
    'spawnve',
    'spawnvp',
    'spawnvpe',
    'system',
    'waitpid',
    'write',
]

name = 'posix'
sep = '/'
linesep = '\n'
curdir = '.'
pardir = '..'
pathsep = ':'
environ = posix.environ()
WEXITSTATUS = posix.WEXITSTATUS
WIFEXITED = posix.WIFEXITED
WNOHANG = posix.WNOHANG
O_APPEND = posix.O_APPEND
O_CREAT = posix.O_CREAT
O_RDONLY = posix.O_RDONLY
O_RDWR = posix.O_RDWR
O_TRUNC = posix.O_TRUNC
O_WRONLY = posix.O_WRONLY
P_NOWAIT = posix.P_NOWAIT
P_WAIT = posix.P_WAIT
_exit = posix._exit
close = posix.close
dup = posix.dup
dup2 = posix.dup2
execv = posix.execv
execve = posix.execve
execvp = posix.execvp
execvpe = posix.execvpe
fork = posix.fork
getcwd = posix.getcwd
getpid = posix.getpid
kill = posix.kill
listdir = posix.listdir
open = posix.open
pipe = posix.pipe
read = posix.read
system = posix.system
waitpid = posix.waitpid
write = posix.write


def execl(path, *args):
    """Execute path with argument list args, replacing the current process."""
    return execv(path, args)


def execle(path, *args):
    """Execute path with argument list args and final environment mapping."""
    if len(args) < 1:
        raise TypeError('execle() missing environment')
    return execve(path, args[:-1], args[-1])


def execlp(file, *args):
    """Execute file with PATH lookup and argument list args."""
    return execvp(file, args)


def execlpe(file, *args):
    """Execute file with PATH lookup, argument list args, and environment."""
    if len(args) < 1:
        raise TypeError('execlpe() missing environment')
    return execvpe(file, args[:-1], args[-1])


def _check_spawn_mode(mode):
    if mode != P_WAIT and mode != P_NOWAIT:
        raise ValueError('spawn mode must be os.P_WAIT or os.P_NOWAIT')


def _wait_spawn(pid):
    waited = waitpid(pid, 0)
    status = waited[1]
    if WIFEXITED(status):
        return WEXITSTATUS(status)
    return status


def spawnv(mode, path, args):
    """Execute path in a child process using an argument vector."""
    _check_spawn_mode(mode)
    pid = fork()
    if pid == 0:
        try:
            execv(path, args)
        except BaseException:
            _exit(127)
    if mode == P_NOWAIT:
        return pid
    return _wait_spawn(pid)


def spawnve(mode, path, args, env):
    """Execute path in a child process with an explicit environment."""
    _check_spawn_mode(mode)
    pid = fork()
    if pid == 0:
        try:
            execve(path, args, env)
        except BaseException:
            _exit(127)
    if mode == P_NOWAIT:
        return pid
    return _wait_spawn(pid)


def spawnvp(mode, file, args):
    """Execute file in a child process using PATH lookup."""
    _check_spawn_mode(mode)
    pid = fork()
    if pid == 0:
        try:
            execvp(file, args)
        except BaseException:
            _exit(127)
    if mode == P_NOWAIT:
        return pid
    return _wait_spawn(pid)


def spawnvpe(mode, file, args, env):
    """Execute file in a child process using PATH lookup and env."""
    _check_spawn_mode(mode)
    pid = fork()
    if pid == 0:
        try:
            execvpe(file, args, env)
        except BaseException:
            _exit(127)
    if mode == P_NOWAIT:
        return pid
    return _wait_spawn(pid)


def spawnl(mode, path, *args):
    """Execute path in a child process using argument list args."""
    return spawnv(mode, path, args)


def spawnle(mode, path, *args):
    """Execute path in a child process using args and final env mapping."""
    if len(args) < 1:
        raise TypeError('spawnle() missing environment')
    return spawnve(mode, path, args[:-1], args[-1])


def spawnlp(mode, file, *args):
    """Execute file in a child process using PATH lookup and args."""
    return spawnvp(mode, file, args)


def spawnlpe(mode, file, *args):
    """Execute file in a child process using PATH lookup, args, and env."""
    if len(args) < 1:
        raise TypeError('spawnlpe() missing environment')
    return spawnvpe(mode, file, args[:-1], args[-1])
