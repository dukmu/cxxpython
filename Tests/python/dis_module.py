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


instructions = list(dis.get_instructions(sample))
first = instructions[0]

print(type(dis))
print(dis.__all__.__contains__('get_instructions'))
print(dis.__all__.__contains__('Instruction'))
print(inspect.getdoc(dis).__contains__('cxxpython bytecode'))
print(inspect.getdoc(dis.get_instructions).__contains__('instructions'))
print(type(first))
print(first.opname)
print(type(first.opcode))
print(type(first.arg))
print(first.offset)
print(first.starts_line is None or first.starts_line > 0)
print(instructions[-1].opname)

writer = Writer()
print(dis.dis(sample, file=writer))
print(writer.text.__contains__('LOAD_CONST'))
print(writer.text.__contains__('RETURN_VALUE'))
print(list(sample.__code__._cxx_instructions())[0][1] == first.opname)
