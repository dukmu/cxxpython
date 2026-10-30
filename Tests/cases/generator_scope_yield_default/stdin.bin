try:
    compile('def f(x=(yield 1)): pass\n', 'case', 'exec')
except SyntaxError as error:
    print(type(error).__name__, error.lineno)
