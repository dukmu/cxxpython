import gc
import types

module = types.ModuleType('example.module')

def missing(name):
    try:
        getattr(module, name)
    except Exception as error:
        print(type(error).__name__, str(error))

missing('feature')
module.__name__ = "renamed'quoted"
missing("not'present")
del module.__name__
missing('feature')
module.__name__ = 42
missing('feature')
module.__name__ = 'example.module'
module.__spec__ = types.SimpleNamespace(_initializing=True)
missing('feature')
module.__spec__ = types.SimpleNamespace(_initializing=False,
                                       _uninitialized_submodules=['child'])
missing('child')
missing('other')

class NoisySpec:
    @property
    def _initializing(self):
        gc.collect()
        raise ValueError('diagnostic only')

    @property
    def _uninitialized_submodules(self):
        gc.collect()
        raise ValueError('diagnostic only')

module.__spec__ = NoisySpec()
missing('feature')
class ActiveSpec:
    @property
    def _initializing(self):
        gc.collect()
        return True

module.__spec__ = ActiveSpec()
missing('feature')

def hook(name):
    gc.collect()
    if name == 'provided':
        return 7
    raise AttributeError('from user hook: ' + name)

module.__getattr__ = hook
print(module.provided)
missing('feature')
