import os
import posix


print(os.__all__.__contains__('fork'))
print(os.__all__.__contains__('waitpid'))
print(os.__all__.__contains__('pipe'))
print(posix.WNOHANG == os.WNOHANG)

fds = os.pipe()
print(type(fds))
print(len(fds))
read_fd = fds[0]
write_fd = fds[1]
dup_fd = os.dup(read_fd)
print(type(dup_fd))
print(dup_fd >= 0)
print(os.dup2(read_fd, dup_fd) == dup_fd)
print(os.close(dup_fd))
payload = bytes([111, 107])
print(os.write(write_fd, payload))
print(os.close(write_fd))
print(os.read(read_fd, 2) == payload)
print(os.close(read_fd))

pid = os.fork()
if pid == 0:
    os._exit(7)

waited = os.waitpid(pid, 0)
print(type(pid))
print(pid > 0)
print(type(waited))
print(waited[0] == pid)
print(os.WIFEXITED(waited[1]))
print(os.WEXITSTATUS(waited[1]) == 7)

try:
    os.waitpid('child', 0)
except TypeError as exc:
    print(type(exc))

try:
    os.close(-1)
except OSError as exc:
    print(type(exc))
