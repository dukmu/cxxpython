import marshal
import types


def outer(parameter):
    local = 10

    def inner(value):
        return parameter + local + value

    return inner


original = outer.__code__
encoded = marshal.dumps(original)
restored = marshal.loads(encoded)
print(type(restored) is types.CodeType)
print(restored.co_argcount == original.co_argcount)
print(restored.co_varnames == original.co_varnames)
print(restored.co_cellvars == original.co_cellvars)
print(restored.co_freevars == original.co_freevars)
print(restored.co_code == original.co_code)
print(restored.co_linetable == original.co_linetable)
print(restored.co_exceptiontable == original.co_exceptiontable)
restored_inner = types.FunctionType(restored, globals())(5)
print(restored_inner(7))
print(restored_inner.__code__.co_freevars == outer(5).__code__.co_freevars)

try:
    marshal.loads(encoded[:20])
except EOFError:
    print('EOFError')
