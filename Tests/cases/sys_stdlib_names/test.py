import sys

names = sys.stdlib_module_names
print(type(names) is frozenset, all(type(name) is str for name in names))
print(all(name in names for name in ('sys', 'math', 'code', 'json', 'threading', 'xml')))
print(all('.' not in name for name in names), 'test' not in names, 'dill' not in names)
try:
    missing_name
except NameError as error:
    import traceback
    print(traceback.format_exception_only(error)[-1].strip())
