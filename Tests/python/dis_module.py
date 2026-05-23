import dis
import inspect


class Writer:
    def __init__(self):
        self.text = ''

    def write(self, text):
        self.text = self.text + text

    def flush(self):
        pass


def sample(value):
    result = value + 1
    return result


def choose(value):
    if value < 3:
        return 1
    return 2


instructions = list(dis.get_instructions(sample))
first = instructions[0]
choose_instructions = list(dis.get_instructions(choose))
jump_instruction = None
jump_target = None
const_instruction = None
compare_instruction = None
binary_instruction = None
for instruction in choose_instructions:
    if instruction.opname == 'POP_JUMP_IF_FALSE':
        jump_instruction = instruction
    if instruction.is_jump_target:
        jump_target = instruction
    if instruction.opname == 'LOAD_CONST' and const_instruction is None:
        const_instruction = instruction
    if instruction.opname == 'COMPARE_OP':
        compare_instruction = instruction
for instruction in instructions:
    if instruction.opname == 'BINARY_OP':
        binary_instruction = instruction

print(type(dis))
print(dis.__all__.__contains__('get_instructions'))
print(dis.__all__.__contains__('Instruction'))
print(dis.__all__.__contains__('Bytecode'))
print(inspect.getdoc(dis).__contains__('cxxpython bytecode'))
print(inspect.getdoc(dis.get_instructions).__contains__('instructions'))
print(type(first))
print(first.opname)
print(type(first.opcode))
print(type(first.arg))
print(first.offset)
print(first.starts_line is None or first.starts_line > 0)
print(first.argval == 'value')
print(first.argrepr == 'value')
print(instructions[-1].opname)
print(jump_instruction.argval == jump_target.offset)
print(jump_instruction.argrepr == 'to ' + str(jump_target.offset))
print(jump_target.is_jump_target)
print(const_instruction.argval == 3)
print(const_instruction.argrepr == '3')
print(binary_instruction.argval == '+')
print(binary_instruction.argrepr == '+')
print(compare_instruction.argval == '<')
print(compare_instruction.argrepr == '<')

writer = Writer()
print(dis.dis(sample, file=writer))
print(writer.text.__contains__('LOAD_CONST'))
print(writer.text.__contains__('RETURN_VALUE'))
print(writer.text.__contains__('BINARY_OP'))
print(writer.text.__contains__('(+)'))
choose_writer = Writer()
dis.dis(choose, file=choose_writer)
print(choose_writer.text.__contains__('>>'))
print(choose_writer.text.__contains__('to '))
print(choose_writer.text.__contains__('COMPARE_OP'))
print(choose_writer.text.__contains__('(<)'))
bytecode = dis.Bytecode(sample)
print(type(bytecode))
print(list(bytecode)[0].opname == first.opname)
print(bytecode.dis().__contains__('LOAD_FAST'))
print(list(sample.__code__._cxx_instructions())[0][1] == first.opname)
