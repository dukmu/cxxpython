import linecache
import pdb

source = """\
def child(value):
    result = value + 1
    return result

def target():
    value = 10
    value = child(value)
    print('result:', value)
"""
filename = '<pdb-interactive>'
linecache.cache[filename] = (len(source), None, source.splitlines(True), filename)
namespace = {}
exec(compile(source, filename, 'exec'), namespace)
pdb.Pdb(nosigint=True, readrc=False).runcall(namespace['target'])
