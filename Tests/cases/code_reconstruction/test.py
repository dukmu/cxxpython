import gc
import types
import warnings

warnings.simplefilter("ignore", DeprecationWarning)

fields = (
    "co_argcount", "co_posonlyargcount", "co_kwonlyargcount", "co_nlocals",
    "co_stacksize", "co_flags", "co_code", "co_consts", "co_names", "co_varnames",
    "co_filename", "co_name", "co_qualname", "co_firstlineno", "co_linetable",
    "co_exceptiontable", "co_freevars", "co_cellvars",
)


def reconstruct(code):
    return types.CodeType(*(getattr(code, name) for name in fields))


events = []


def target(a, /, b=3, *args, scale=2, **kwargs):
    """kept doc"""
    try:
        result = a + b + sum(args) + kwargs.get("extra", 0)
        if a < 0:
            raise ValueError("negative")
        return dict(result=result * scale)
    except ValueError as error:
        return str(error)
    finally:
        events.append("finally")


code = reconstruct(target.__code__)
clone = types.FunctionType(code, globals(), "clone", target.__defaults__)
clone.__kwdefaults__ = target.__kwdefaults__
gc.collect()
print(clone(1, 4, 5, scale=3, extra=2), clone(-1), clone.__doc__)
print(events)


def outer(value):
    def inner(other):
        return value + other
    return inner


closure = outer(40)
copy = types.FunctionType(reconstruct(closure.__code__), globals(), closure=closure.__closure__)
print("closure", copy(2))
copy_outer = types.FunctionType(reconstruct(outer.__code__), globals())
print("outer", copy_outer(10)(2))
no_lines = outer.__code__.replace(co_linetable=b"")
print("no locations", all(row[0] is None for row in no_lines.co_positions()),
      types.FunctionType(no_lines, globals())(40)(2))
legacy_lines = outer.__code__.replace(co_linetable=outer.__code__.co_lnotab)
print("legacy locations", types.FunctionType(legacy_lines, globals())(40)(2))


def generator():
    yield 1
    yield 2


copy_generator = types.FunctionType(reconstruct(generator.__code__), globals())
print("generator", list(copy_generator()))
namespace = {}
exec(reconstruct(compile("answer = 6 * 7", "<module>", "exec")), namespace)
print("module", namespace["answer"])
renamed = target.__code__.replace(co_name="renamed", co_filename="<copy>",
                                  co_firstlineno=target.__code__.co_firstlineno + 20)
original_lines = [position[0] for position in target.__code__.co_positions()]
new_lines = [position[0] for position in renamed.co_positions()]
assert new_lines == [None if line is None else line + 20 for line in original_lines]
print(renamed.co_name, renamed.co_filename)
bad = [getattr(code, name) for name in fields]
bad[0] = -1
try:
    types.CodeType(*bad)
except ValueError:
    print("negative rejected")
bad = [getattr(code, name) for name in fields]
bad[6] = bad[6][:-1]
try:
    types.CodeType(*bad)
except ValueError:
    print("truncated rejected")
