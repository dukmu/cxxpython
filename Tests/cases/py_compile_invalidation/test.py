import importlib.machinery
import importlib.util
import py_compile

source = 'invalidation_subject.py'
with open(source, 'w') as stream:
    stream.write('value = 1\n')

def value():
    loader = importlib.machinery.SourceFileLoader('invalidation_subject', source)
    spec = importlib.util.spec_from_loader('invalidation_subject', loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module.value

py_compile.compile(source, invalidation_mode=py_compile.PycInvalidationMode.CHECKED_HASH)
with open(source, 'w') as stream:
    stream.write('value = 2\n')
print(value())
py_compile.compile(source, invalidation_mode=py_compile.PycInvalidationMode.UNCHECKED_HASH)
with open(source, 'w') as stream:
    stream.write('value = 3\n')
print(value())
