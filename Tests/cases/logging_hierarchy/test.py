import io
import logging

stream = io.StringIO()
handler = logging.StreamHandler(stream)
handler.setFormatter(logging.Formatter('%(name)s:%(levelname)s:%(message)s:%(tag)s'))
handler.setLevel(logging.INFO)
root = logging.getLogger('closure')
root.setLevel(logging.WARNING)
root.addHandler(handler)
child = logging.getLogger('closure.branch.leaf')
branch = logging.getLogger('closure.branch')
print(child.parent is branch, branch.parent is root)
print(child.getEffectiveLevel(), child.isEnabledFor(logging.INFO))
branch.setLevel(logging.DEBUG)
print(child.getEffectiveLevel(), child.isEnabledFor(logging.INFO))

class Replace(logging.Filter):
    def filter(self, record):
        if record.msg == 'drop':
            return False
        record.tag = 'filtered'
        return record

handler.addFilter(Replace())
old_factory = logging.getLogRecordFactory()
def factory(*args, **kwargs):
    record = old_factory(*args, **kwargs)
    record.tag = 'factory'
    return record
logging.setLogRecordFactory(factory)
adapter = logging.LoggerAdapter(child, {'context': 'adapter'})
adapter.debug('hidden')
adapter.info('hello %s', 'world')
adapter.warning('drop')
logging.disable(logging.ERROR)
adapter.error('disabled')
logging.disable(logging.NOTSET)
child.propagate = False
null = logging.NullHandler()
child.addHandler(null)
adapter.error('not propagated')
child.removeHandler(null)
child.propagate = True
print(stream.getvalue(), end='')
print(adapter.name, adapter.isEnabledFor(logging.DEBUG))
print(logging.getLevelName('WARNING'), logging.getLevelName(17))
logging.setLogRecordFactory(old_factory)
root.removeHandler(handler)
handler.close()
