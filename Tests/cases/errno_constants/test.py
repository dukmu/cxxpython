import errno

print(errno.ENOENT, errno.EBADF, errno.ENOTTY)
print(errno.errorcode[errno.ENOENT], errno.errorcode[errno.EBADF], errno.errorcode[errno.ENOTTY])
print(errno.errorcode[errno.EAGAIN] == "EAGAIN" or errno.errorcode[errno.EAGAIN] == "EWOULDBLOCK")
