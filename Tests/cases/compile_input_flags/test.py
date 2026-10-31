import __future__
from codeop import CommandCompiler, Compile, compile_command

sources = ('x = 1', 'x =', 'x = (', 'x = """', 'x = "a',
           'if True:\n pass', 'if True:\n pass\n', 'if True:\n pass\n\n',
           'def f():\n return 4', 'if True:\n', 'a = 1\nb = 2\n')
for source in sources:
    for mode in ('single', 'exec', 'eval'):
        for flags in (0, 512, 16384, 16896):
            try:
                code = compile(source, 'flags.py', mode, flags)
                result = ('code', bool(code.co_flags & 16896))
            except SyntaxError as error:
                result = (type(error).__name__, 'incomplete input' in str(error))
            print(repr(source), mode, flags, result)

for text in ('', '\n', '# comment', ' \n# comment\n'):
    print('empty command', repr(text), compile_command(text) is not None)

compiler = CommandCompiler()
code = compiler('from __future__ import annotations')
print('future initial', bool(code.co_flags & __future__.annotations.compiler_flag))
code = compiler('def f(value: Unknown) -> Unknown:\n return value\n')
space = {}
exec(code, space)
print('future remembered', space['f'].__annotations__)
print('incomplete', compiler('def g():') is None)
print('subsequent', compiler('value = 7') is not None)

direct = Compile()
print('exec incomplete dedent ignored', direct('if True:\n pass', 'x', 'exec') is not None)
print('flags not inherited', bool(compile('pass', 'x', 'exec').co_flags & 16896))
