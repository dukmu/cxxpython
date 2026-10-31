import dis
import io
import opcode

def sample(value):
    if value:
        return len((value,))
    return 0

instructions = list(dis.get_instructions(sample))
assert len(instructions) > 0
assert [i.offset for i in instructions] == list(range(len(instructions)))
assert all(opcode.opname[i.opcode] == i.opname for i in instructions)
assert any(i.is_jump_target for i in instructions)
assert list(dis.get_instructions(sample, show_caches=True)) == instructions
assert list(dis.get_instructions(sample, adaptive=True)) == instructions
calls = [i for i in instructions if i.opname == "CALL"]
assert len(calls) == 1
bytecode = dis.Bytecode(sample, current_offset=calls[0].offset)
assert bytecode.stack_effect(calls[0].offset) == -1
assert "-->" in bytecode.dis()
try:
    opcode.stack_effect(calls[0].opcode, calls[0].arg)
except ValueError:
    pass
else:
    assert False
assert "sample" in dis.code_info(sample)
assert "CALL" in bytecode.dis()
assert list(dis.findlinestarts(sample)) == list(dis.find_line_starts(sample))
assert set(dis.findlabels(sample)) == set(i.offset for i in instructions if i.is_jump_target)
output = io.StringIO()
dis.disassemble(sample.__code__, calls[0].offset, file=output)
assert "-->" in output.getvalue()

def outer():
    def inner():
        return 1
    return inner

output = io.StringIO()
dis.dis(outer, file=output, depth=0)
assert "Disassembly of" not in output.getvalue()
output = io.StringIO()
dis.dis(outer, file=output, depth=1)
assert "Disassembly of" in output.getvalue()
output = io.StringIO()
dis.show_code(sample, file=output)
assert "Argument count:" in output.getvalue()
assert list(dis.get_instructions(sample.__code__)) == instructions
shifted = list(dis.get_instructions(sample, first_line=200))
delta = 200 - sample.__code__.co_firstlineno
for before, after in zip(instructions, shifted):
    assert after.positions.lineno == (None if before.positions.lineno is None
                                     else before.positions.lineno + delta)
assert len(list(dis.get_instructions("1 + 2"))) > 0

class Example:
    def method(self):
        return 1

assert len(list(dis.get_instructions(Example().method))) > 0
output = io.StringIO()
dis.dis(Example, file=output)
assert "Disassembly of method:" in output.getvalue()

def generator():
    yield 1

assert len(list(dis.get_instructions(generator()))) > 0

async def async_loop(it):
    async for item in it:
        pass

async_instructions = list(dis.get_instructions(async_loop))
awaits = [i for i in async_instructions if i.opname == "FOR_AWAIT"]
assert len(awaits) == 1
async_bytecode = dis.Bytecode(async_loop)
assert async_bytecode.stack_effect(awaits[0].offset, jump=False) == 1
assert async_bytecode.stack_effect(awaits[0].offset, jump=True) == 0

def grouped():
    try:
        raise ExceptionGroup('group', [ValueError('v')])
    except* ValueError:
        pass

grouped_ops = list(dis.get_instructions(grouped))
assert any(i.opname == 'CHECK_EG_MATCH' for i in grouped_ops)
assert any(i.opname == 'PREP_RERAISE_STAR' for i in grouped_ops)
assert any(i.opname == 'RERAISE' and i.arg == 1 for i in grouped_ops)
assert opcode.stack_effect(opcode.opmap['CHECK_EG_MATCH']) == 0
assert opcode.stack_effect(opcode.opmap['PREP_RERAISE_STAR']) == -1
assert opcode.stack_effect(opcode.opmap['RERAISE'], 0) == 0
assert opcode.stack_effect(opcode.opmap['RERAISE'], 1) == -1
try:
    opcode.stack_effect(opcode.opmap['RERAISE'], 2)
except ValueError:
    pass
else:
    assert False

def matching(subject):
    match subject:
        case [_, _]:
            return None
        case {"key": _, **rest}:
            return rest
        case Example(_, _):
            return None

match_ops = list(dis.get_instructions(matching))
match_bytecode = dis.Bytecode(matching)
match_listing = match_bytecode.dis()
for name, effect in (("MATCH_SEQUENCE", 1), ("MATCH_MAPPING", 1),
                     ("GET_LEN", 1), ("MATCH_KEYS", 1),
                     ("MATCH_CLASS", -2), ("COPY_DICT_WITHOUT_KEYS", -1)):
    found = [i for i in match_ops if i.opname == name]
    assert found
    assert name in match_listing
    for instruction in found:
        assert opcode.opname[instruction.opcode] == name
        assert opcode.opmap[name] == instruction.opcode
        assert match_bytecode.stack_effect(instruction.offset) == effect
        if name == "MATCH_CLASS":
            assert instruction.arg == 2
            assert opcode.stack_effect(instruction.opcode, instruction.arg) == effect
        else:
            assert opcode.stack_effect(instruction.opcode) == effect

class Index:
    def __init__(self, value):
        self.value = value

    def __index__(self):
        return self.value

class IntSubclass(int):
    pass

load_const = opcode.opmap["LOAD_CONST"]
expected = opcode.stack_effect(load_const, 0)
assert opcode.stack_effect(Index(load_const), Index(0)) == expected
assert opcode.stack_effect(IntSubclass(load_const), IntSubclass(0)) == expected
assert opcode.stack_effect(load_const, False) == expected
if opcode._domains[0] == "None":
    assert opcode.stack_effect(False) == opcode.stack_effect(0)
elif opcode._domains[0] != "CallSite":
    assert opcode.stack_effect(False, False) == opcode.stack_effect(0, 0)
for operation, argument in ((float(load_const), 0), (load_const, 0.0)):
    try:
        opcode.stack_effect(operation, argument)
    except TypeError:
        pass
    else:
        assert False
