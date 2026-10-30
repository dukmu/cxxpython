try:
    compile('def f():\n    return [(yield x) for x in ()]\n', 'case', 'exec')
except SyntaxError as error:
    print(type(error).__name__, error.lineno)
