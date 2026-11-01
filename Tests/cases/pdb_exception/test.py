import linecache
import pdb

source = """\
def target():
    try:
        raise ValueError('traced failure')
    except ValueError:
        print('handled')
"""
filename = '<pdb-exception>'
linecache.cache[filename] = (len(source), None, source.splitlines(True), filename)
namespace = {}
exec(compile(source, filename, 'exec'), namespace)
pdb.Pdb(nosigint=True, readrc=False).runcall(namespace['target'])
