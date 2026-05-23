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

__all__ = ['Bytecode', 'Instruction', 'dis', 'get_instructions']

_JUMP_OPNAMES = [
    'FOR_ITER',
    'BREAK_LOOP',
    'CONTINUE_LOOP',
    'JUMP_ABSOLUTE',
    'POP_JUMP_IF_FALSE',
    'POP_JUMP_IF_TRUE',
    'JUMP_IF_FALSE_OR_POP',
    'SETUP_EXCEPT',
    'SETUP_FINALLY',
]

_NAME_OPNAMES = [
    'LOAD_NAME',
    'STORE_NAME',
    'DELETE_NAME',
    'LOAD_GLOBAL',
    'STORE_GLOBAL',
    'DELETE_GLOBAL',
    'LOAD_ATTR',
    'STORE_ATTR',
    'DELETE_ATTR',
]

_VARNAME_OPNAMES = [
    'LOAD_FAST',
    'STORE_FAST',
    'DELETE_FAST',
]

_DEREF_OPNAMES = [
    'LOAD_DEREF',
    'STORE_DEREF',
    'DELETE_DEREF',
    'LOAD_CLOSURE',
]

_BINARY_OP_NAMES = ['+', '-', '*', '/', '//', '%', '**']
_UNARY_OP_NAMES = ['+', '-', '~', 'not']
_COMPARE_OP_NAMES = ['==', '!=', '<', '<=', '>', '>=', 'is', 'is not', 'in', 'not in']


def _get_code_object(x):
    try:
        return x.codeobj
    except AttributeError:
        pass
    try:
        return x.__code__
    except AttributeError:
        return x


def _safe_index(values, index):
    if index < 0:
        return None
    if index >= len(values):
        return None
    return values[index]


def _deref_names(code):
    return code.co_cellvars + code.co_freevars


def _resolve_arg(code, opname, arg):
    if opname == 'LOAD_CONST':
        value = _safe_index(code.co_consts, arg)
        return (value, repr(value))
    if opname in _NAME_OPNAMES:
        value = _safe_index(code.co_names, arg)
        return (value, str(value))
    if opname in _VARNAME_OPNAMES:
        value = _safe_index(code.co_varnames, arg)
        return (value, str(value))
    if opname in _DEREF_OPNAMES:
        value = _safe_index(_deref_names(code), arg)
        return (value, str(value))
    if opname == 'BINARY_OP' or opname == 'INPLACE_BINARY_OP':
        value = _safe_index(_BINARY_OP_NAMES, arg)
        if value is None:
            return (arg, str(arg))
        return (value, value)
    if opname == 'UNARY_OP':
        value = _safe_index(_UNARY_OP_NAMES, arg)
        if value is None:
            return (arg, str(arg))
        return (value, value)
    if opname == 'COMPARE_OP':
        value = _safe_index(_COMPARE_OP_NAMES, arg)
        if value is None:
            return (arg, str(arg))
        return (value, value)
    if opname == 'FORMAT_VALUE':
        return (arg, '')
    if opname in _JUMP_OPNAMES:
        return (arg, 'to ' + str(arg))
    return (arg, str(arg))


def _jump_targets(raw_instructions):
    targets = []
    for raw in raw_instructions:
        if raw[1] in _JUMP_OPNAMES:
            targets.append(raw[3])
    return targets


def _instruction_from_private(code, raw, jump_targets):
    arg = raw[3]
    resolved = _resolve_arg(code, raw[1], arg)
    argval = resolved[0]
    argrepr = resolved[1]
    return Instruction(
        raw[1],
        raw[2],
        arg,
        argval,
        argrepr,
        raw[0],
        raw[4],
        raw[0] in jump_targets,
        None,
    )


def get_instructions(x):
    """Return an iterator over instructions for a code object or function."""
    code = _get_code_object(x)
    raw_instructions = list(code._cxx_instructions())
    jump_targets = _jump_targets(raw_instructions)
    instructions = []
    for raw in raw_instructions:
        instructions.append(_instruction_from_private(code, raw, jump_targets))
    return iter(instructions)


def _pad_left(text, width):
    text = str(text)
    while len(text) < width:
        text = ' ' + text
    return text


def _pad_right(text, width):
    text = str(text)
    while len(text) < width:
        text = text + ' '
    return text


def _format_instruction(instruction):
    line = ''
    if instruction.starts_line is not None:
        line = str(instruction.starts_line)
    marker = '  '
    if instruction.is_jump_target:
        marker = '>>'
    text = _pad_left(line, 3) + ' ' + marker + ' '
    text = text + _pad_left(instruction.offset, 4) + ' '
    text = text + _pad_right(instruction.opname, 22)
    if instruction.arg is not None:
        text = text + _pad_left(instruction.arg, 4)
        if instruction.argrepr:
            text = text + ' (' + instruction.argrepr + ')'
    return text


def dis(x=None, file=None):
    """Disassemble a code object or function."""
    if x is None:
        return None
    if file is None:
        file = sys.stdout
    for instruction in get_instructions(x):
        file.write(_format_instruction(instruction) + '\n')
    file.flush()
    return None


class Bytecode:
    """Bytecode analysis for a function or code object."""

    def __init__(self, x):
        self.codeobj = _get_code_object(x)

    def __iter__(self):
        return get_instructions(self.codeobj)

    def dis(self):
        result = ''
        for instruction in self:
            result = result + _format_instruction(instruction) + '\n'
        return result
