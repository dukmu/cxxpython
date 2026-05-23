"""Disassemble cxxpython bytecode.

This module follows CPython's script-level ``dis`` layering, but the opcode
numbers and offsets describe cxxpython's private instruction stream.  They are
not CPython bytecode numbers.
"""

import sys
from collections import namedtuple

Instruction = namedtuple(
    'Instruction',
    'opname opcode arg argval argrepr offset starts_line is_jump_target positions',
)

__all__ = ['Instruction', 'dis', 'get_instructions']


def _get_code_object(x):
    try:
        return x.__code__
    except AttributeError:
        return x


def _instruction_from_private(raw):
    arg = raw[3]
    return Instruction(
        raw[1],
        raw[2],
        arg,
        arg,
        str(arg),
        raw[0],
        raw[4],
        False,
        None,
    )


def get_instructions(x):
    """Return an iterator over instructions for a code object or function."""
    code = _get_code_object(x)
    instructions = []
    for raw in code._cxx_instructions():
        instructions.append(_instruction_from_private(raw))
    return iter(instructions)


def dis(x=None, file=None):
    """Disassemble a code object or function."""
    if x is None:
        return None
    if file is None:
        file = sys.stdout
    for instruction in get_instructions(x):
        line = ''
        if instruction.starts_line is not None:
            line = str(instruction.starts_line)
        file.write(str(instruction.offset) + ' ' + line + ' ' +
                   instruction.opname + ' ' + instruction.argrepr + '\n')
    file.flush()
    return None
