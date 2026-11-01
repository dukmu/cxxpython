import contextlib
import dis
import io
import os
import runpy
import sys
import warnings

for instruction in (
    dis.Instruction('LOAD_NAME', 1, 2, 'value', 'value', 12, 1234, True),
    dis.Instruction('JUMP', 2, 8, 8, 'to 8', 13, None, False),
    dis.Instruction('RETURN', 3, None, None, '', 14, None, False),
):
    print(instruction._disassemble(4, instruction.offset == 13, 5))

source = 'value = 1\nprint(value)\n'
expected = dis.Bytecode(compile(source, '<stdin>', 'exec')).dis()
output = io.StringIO()
sys.argv = ['dis']
with warnings.catch_warnings():
    warnings.simplefilter('ignore', RuntimeWarning)
    with contextlib.redirect_stdout(output):
        runpy.run_module('dis', run_name='__main__', alter_sys=True)
assert output.getvalue() == expected
print('dis module stdin matches Bytecode')

path = os.path.join(os.path.dirname(__file__), 'stdin.bin')
with open(path, 'rb') as infile:
    expected = dis.Bytecode(compile(infile.read(), path, 'exec')).dis()
output = io.StringIO()
sys.argv = ['dis', path]
with warnings.catch_warnings():
    warnings.simplefilter('ignore', RuntimeWarning)
    with contextlib.redirect_stdout(output):
        runpy.run_module('dis', run_name='__main__', alter_sys=True)
assert output.getvalue() == expected
print('dis module file matches Bytecode')

code = compile(source, '<format>', 'exec')
instructions = list(dis.get_instructions(code))
name = next(item for item in instructions if item.opname == 'STORE_NAME')
assert name.argval == 'value' and name.argrepr == 'value'
assert tuple(name.positions) == (1, 1, 0, 5)
assert '\n\n' in dis.Bytecode(code).dis()
branches = list(dis.get_instructions(compile('if value:\n    print(value)\n', '<jump>', 'exec')))
jumps = [item for item in branches if item.opcode in dis.hasjabs + dis.hasjrel]
assert jumps and all(item.argrepr == 'to ' + repr(item.argval) for item in jumps)
assert all(any(target.offset == jump.argval and target.is_jump_target for target in branches)
           for jump in jumps)
print('dis name and source-line formatting ok')
