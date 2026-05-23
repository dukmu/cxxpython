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


def _get_code_object(x):
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
        marker = '  '
        if instruction.is_jump_target:
            marker = '>>'
        file.write(marker + ' ' + str(instruction.offset) + ' ' + line + ' ' +
                   instruction.opname + ' ' + instruction.argrepr + '\n')
    file.flush()
    return None
