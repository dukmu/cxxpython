import __future__

flag = 16777216
print(__future__.annotations.compiler_flag == flag)
print([(name, getattr(__future__, name).compiler_flag != 0)
       for name in __future__.all_feature_names if name != 'barry_as_FLUFL'])
source = 'x: missing\n'
for flags in (0, flag):
    code = compile(source, '<test>', 'exec', flags=flags, dont_inherit=True)
    namespace = {}
    try:
        exec(code, namespace)
        print(bool(code.co_flags & flag), namespace['__annotations__'])
    except NameError:
        print(bool(code.co_flags & flag), 'NameError')

caller = compile('from __future__ import annotations\n'
                 'def run(precompiled):\n'
                 ' ns = {}\n'
                 ' exec("x: missing", ns)\n'
                 ' print(ns["__annotations__"])\n'
                 ' print(bool(compile("0", "<test>", "eval").co_flags & 16777216))\n'
                 ' print(bool(compile("0", "<test>", "eval", dont_inherit=True).co_flags & 16777216))\n'
                 ' print(eval("(lambda x: x).__code__.co_flags") & 16777216 != 0)\n'
                 ' print(eval(precompiled) & 16777216 != 0)\n', '<test>', 'exec')
namespace = {}
exec(caller, namespace)
precompiled = compile('(lambda x: x).__code__.co_flags', '<test>', 'eval', dont_inherit=True)
namespace['run'](precompiled)
for source in ('x = 1\nfrom __future__ import annotations',
               'from __future__ import imaginary_feature'):
    try:
        compile(source, '<test>', 'exec', dont_inherit=True)
    except SyntaxError as error:
        print(type(error).__name__, error.msg)
code = compile('"doc"\nfrom __future__ import annotations\nx: int', '<test>', 'exec', dont_inherit=True)
print(bool(code.co_flags & flag))
