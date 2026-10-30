try:
    compile('class C:\n    yield 1\n', 'case', 'exec')
except SyntaxError as error:
    print(type(error).__name__, error.lineno)
