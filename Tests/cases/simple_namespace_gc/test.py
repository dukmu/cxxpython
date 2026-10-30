import gc
import sys

SimpleNamespace = type(sys.implementation)

finalized = []

class Derived(SimpleNamespace):
    def __del__(self):
        finalized.append('collected')

ns = Derived()
ns.self = ns
del ns
gc.collect()
assert finalized == ['collected']
print('simple namespace gc ok')
