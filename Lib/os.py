import posix
import posixpath as path

name = 'posix'
sep = '/'
linesep = '\n'
curdir = '.'
pardir = '..'
pathsep = ':'
environ = posix.environ()
getcwd = posix.getcwd
listdir = posix.listdir
