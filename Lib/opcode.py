"""Opcode metadata for cxxpython's private instruction-indexed VM.

Numbers and operands are not CPython bytecode and may change between builds.
"""
import _opcode
from operator import index as _index

_metadata = _opcode.metadata()
opname = [entry[1] for entry in _metadata]
opmap = {name: number for number, name, domain, flow in _metadata}
_domains = [entry[2] for entry in _metadata]

hasconst = [n for n, name, domain, flow in _metadata if domain == "Constant"]
haslocal = [n for n, name, domain, flow in _metadata if domain == "Local"]
hasname = [n for n, name, domain, flow in _metadata if domain == "Name"]
hasjabs = [n for n, name, domain, flow in _metadata if domain == "Target"]
hasjrel = []
hasfree = [n for n, name, domain, flow in _metadata if domain == "Cell"]
__all__ = ["opname", "opmap", "hasconst", "haslocal", "hasname", "hasjabs",
           "hasjrel", "hasfree", "stack_effect"]

def stack_effect(opcode, oparg=None, *, jump=None):
    """Return the actual VM stack effect; call-site operands require code."""
    opcode = _index(opcode)
    if opcode < 0 or opcode >= len(_domains):
        raise ValueError("invalid opcode")
    domain = _domains[opcode]
    if domain == "None":
        if oparg is not None:
            raise ValueError("opcode does not permit an operand")
        oparg = 0
    elif oparg is None:
        raise ValueError("opcode requires an operand")
    else:
        oparg = _index(oparg)
    if jump is not None and jump is not True and jump is not False:
        raise ValueError("stack_effect: jump must be False, True or None")
    if jump is None:
        a = _opcode.stack_effect(opcode, oparg, 0)
        b = _opcode.stack_effect(opcode, oparg, 1)
        return max(a, b)
    return _opcode.stack_effect(opcode, oparg, int(bool(jump)))
