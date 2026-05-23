import io
import inspect
import linecache

print(type(linecache))
print(linecache.__all__.__contains__('getline'))
print(linecache.__all__.__contains__('lazycache'))
print(inspect.getdoc(linecache).__contains__('Cache source lines'))
print(inspect.getdoc(linecache.getline).__contains__('Get a line'))

first = linecache.getline(__file__, 1)
print(first == 'import io\n')
print(linecache.getline(__file__, 0) == '')
print(linecache.getline('/definitely/not/a/cxxpython/source.py', 1) == '')
print(len(linecache.getlines(__file__)) > 5)

linecache.clearcache()
print(linecache.getline(__file__, 2) == 'import inspect\n')

path = '/tmp/cxxpython_linecache_fixture.py'
stream = io.open(path, 'w')
stream.write('alpha\nbeta\n')
stream.close()
print(linecache.getline(path, 2) == 'beta\n')

stream = io.open(path, 'w')
stream.write('changed\n')
stream.close()
print(linecache.getline(path, 1) == 'alpha\n')
linecache.checkcache(path)
print(linecache.getline(path, 1) == 'changed\n')
print(linecache.lazycache(path, globals()))
