"""Disassembly of cxxpython's verified, instruction-indexed private VM.

Policy and public tuple protocols are adapted from CPython 3.12.14 Lib/dis.py
(PSF license). Offsets are instruction indexes; raw CPython wordcode, caches,
and adaptive specializations are not part of this VM.
"""
import collections
import io
import _opcode
import opcode
from opcode import *

__all__ = ["code_info", "dis", "disassemble", "disco", "distb", "findlinestarts",
           "find_line_starts", "findlabels", "show_code", "get_instructions",
           "Instruction", "Positions", "Bytecode"] + opcode.__all__

Positions = collections.namedtuple("Positions", "lineno end_lineno col_offset end_col_offset",
                                   defaults=[None] * 4)
_Instruction = collections.namedtuple(
    "_Instruction", "opname opcode arg argval argrepr offset starts_line is_jump_target positions",
    defaults=[None])
_CodeType = type((lambda: None).__code__)

class Instruction(_Instruction):
    __slots__ = ()

    def _disassemble(self, lineno_width=3, mark_as_current=False, offset_width=4):
        fields = []
        if lineno_width:
            if self.starts_line is None:
                fields.append(" " * lineno_width)
            else:
                fields.append(str(self.starts_line).rjust(lineno_width))
        fields.append("-->" if mark_as_current else "   ")
        fields.append(">>" if self.is_jump_target else "  ")
        fields.append(str(self.offset).rjust(offset_width))
        fields.append(self.opname.ljust(20))
        if self.arg is not None:
            fields.append(str(self.arg).rjust(5))
            if self.argrepr:
                fields.append("(" + self.argrepr + ")")
        return " ".join(fields).rstrip()

def _get_code_object(x):
    if hasattr(x, "__func__"):
        x = x.__func__
    for attr in ("__code__", "gi_code", "ag_code", "cr_code", "f_code"):
        if hasattr(x, attr):
            x = getattr(x, attr)
            break
    if isinstance(x, str):
        try:
            x = compile(x, "<disassembly>", "eval")
        except SyntaxError:
            x = compile(x, "<disassembly>", "exec")
    if not isinstance(x, _CodeType):
        raise TypeError("don't know how to disassemble " + type(x).__name__ + " objects")
    return x

def findlabels(code):
    """Return target instruction indexes in a code object, not wordcode offsets."""
    code = _get_code_object(code)
    labels = []
    seen = set()
    target_ops = set(hasjabs)
    for row in _opcode.instructions(code):
        if row[1] in target_ops and row[2] not in seen:
            seen.add(row[2])
            labels.append(row[2])
    return labels

def findlinestarts(code):
    code = _get_code_object(code)
    previous = None
    for row in _opcode.instructions(code):
        line = row[3]
        if line and line != previous:
            yield row[0], line
            previous = line

find_line_starts = findlinestarts

def get_instructions(x, *, first_line=None, show_caches=False, adaptive=False):
    code = _get_code_object(x)
    rows = _opcode.instructions(code)
    local_names = _opcode.local_names(code)
    targets = set(findlabels(code))
    previous = None
    delta = 0 if first_line is None else first_line - code.co_firstlineno
    for offset, op, operand, line, end_line, col, end_col, fall, jump in rows:
        domain = opcode._domains[op]
        arg = None if domain == "None" else operand
        argval = arg
        if domain == "Constant":
            argval = code.co_consts[operand]
        elif domain == "Local":
            argval = local_names[operand]
        elif domain == "Name":
            argval = code.co_names[operand]
        elif domain == "Cell":
            argval = (code.co_cellvars + code.co_freevars)[operand]
        if line:
            line += delta
            if end_line:
                end_line += delta
            starts = line if line != previous else None
            previous = line
            positions = Positions(line, end_line or None, col, end_col)
        else:
            starts = None
            positions = Positions()
        argrepr = "" if arg is None else repr(argval)
        yield Instruction(opname[op], op, arg, argval, argrepr, offset, starts,
                          offset in targets, positions)

def code_info(x):
    code = _get_code_object(x)
    positional, posonly, kwonly, varargs, varkw, generator, coroutine = _opcode.code_info(code)
    flags = []
    if varargs:
        flags.append("VARARGS")
    if varkw:
        flags.append("VARKEYWORDS")
    if generator:
        flags.append("GENERATOR")
    if coroutine:
        flags.append("COROUTINE")
    lines = ["Name:              " + str(code.co_name),
             "Filename:          " + str(code.co_filename),
             "Argument count:    " + str(positional),
             "Positional-only arguments: " + str(posonly),
             "Kw-only arguments: " + str(kwonly),
             "Number of locals:  " + str(len(code.co_varnames)),
             "Stack size:        " + str(code.co_stacksize),
             "VM traits:         " + ", ".join(flags)]
    for title, values in (("Constants", code.co_consts), ("Names", code.co_names),
                          ("Variable names", code.co_varnames),
                          ("Free variables", code.co_freevars),
                          ("Cell variables", code.co_cellvars)):
        if values:
            lines.append(title + ":")
            for index, value in enumerate(values):
                lines.append(str(index).rjust(4) + ": " + repr(value))
    return "\n".join(lines)

def show_code(co, *, file=None):
    print(code_info(co), file=file)

def disassemble(co, lasti=-1, *, file=None, show_caches=False, adaptive=False):
    for instruction in get_instructions(co, show_caches=show_caches, adaptive=adaptive):
        print(instruction._disassemble(mark_as_current=instruction.offset == lasti), file=file)

disco = disassemble

def _disassemble_recursive(code, *, file=None, depth=None, show_caches=False, adaptive=False):
    disassemble(code, file=file, show_caches=show_caches, adaptive=adaptive)
    if depth is None or depth > 0:
        for constant in code.co_consts:
            if isinstance(constant, type(code)):
                print("\nDisassembly of " + repr(constant) + ":", file=file)
                next_depth = None if depth is None else depth - 1
                _disassemble_recursive(constant, file=file, depth=next_depth,
                                       show_caches=show_caches, adaptive=adaptive)

def dis(x=None, *, file=None, depth=None, show_caches=False, adaptive=False):
    if x is None:
        return distb(file=file, show_caches=show_caches, adaptive=adaptive)
    if hasattr(x, "__dict__") and not hasattr(x, "__code__") and not hasattr(x, "__func__"):
        for name, member in sorted(x.__dict__.items()):
            if hasattr(member, "__code__") or isinstance(member, (type, staticmethod, classmethod)):
                print("Disassembly of " + name + ":", file=file)
                try:
                    dis(member, file=file, depth=depth, show_caches=show_caches, adaptive=adaptive)
                except TypeError as error:
                    print("Sorry:", error, file=file)
                print(file=file)
        return
    code = _get_code_object(x)
    _disassemble_recursive(code, file=file, depth=depth, show_caches=show_caches, adaptive=adaptive)

def distb(tb=None, *, file=None, show_caches=False, adaptive=False):
    if tb is None:
        import sys
        try:
            tb = sys.last_traceback
        except AttributeError:
            raise RuntimeError("no last traceback to disassemble")
    while tb.tb_next:
        tb = tb.tb_next
    disassemble(tb.tb_frame.f_code, tb.tb_lasti, file=file,
                show_caches=show_caches, adaptive=adaptive)

class Bytecode:
    def __init__(self, x, *, first_line=None, current_offset=None, show_caches=False, adaptive=False):
        self.codeobj = _get_code_object(x)
        self.first_line = self.codeobj.co_firstlineno if first_line is None else first_line
        self.current_offset = current_offset
        self.show_caches = show_caches
        self.adaptive = adaptive
        self._original_object = x

    def __iter__(self):
        return get_instructions(self.codeobj, first_line=self.first_line,
                                show_caches=self.show_caches, adaptive=self.adaptive)

    def __repr__(self):
        return self.__class__.__name__ + "(" + repr(self._original_object) + ")"

    @classmethod
    def from_traceback(cls, tb, *, show_caches=False, adaptive=False):
        while tb.tb_next:
            tb = tb.tb_next
        return cls(tb.tb_frame.f_code, current_offset=tb.tb_lasti,
                   show_caches=show_caches, adaptive=adaptive)

    def info(self):
        return code_info(self.codeobj)

    def stack_effect(self, offset, *, jump=None):
        rows = _opcode.instructions(self.codeobj)
        if offset < 0 or offset >= len(rows):
            raise ValueError("invalid instruction offset")
        row = rows[offset]
        if jump is None:
            return max(row[7], row[8])
        return row[8] if jump else row[7]

    def dis(self):
        output = io.StringIO()
        for instruction in self:
            print(instruction._disassemble(mark_as_current=instruction.offset == self.current_offset),
                  file=output)
        return output.getvalue()
