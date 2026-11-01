import linecache
import pdb

source = """\
def fail():
    value = 7
    raise ValueError('debug failure')
"""
filename = '<pdb-postmortem>'
linecache.cache[filename] = (len(source), None, source.splitlines(True), filename)
namespace = {}
exec(compile(source, filename, 'exec'), namespace)
try:
    namespace['fail']()
except ValueError as error:
    pdb.post_mortem(error.__traceback__.tb_next)
