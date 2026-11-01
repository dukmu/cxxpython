import builtins

print(IOError is OSError, EnvironmentError is OSError)
print(builtins.IOError is IOError, builtins.EnvironmentError is EnvironmentError)
print(IOError.__name__, EnvironmentError.__module__)
try:
    raise FileNotFoundError("missing")
except IOError as error:
    print(type(error).__name__, str(error))
