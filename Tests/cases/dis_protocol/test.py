import dis

assert dis.Positions._fields == ("lineno", "end_lineno", "col_offset", "end_col_offset")
p = dis.Positions(1, 2)
assert tuple(p) == (1, 2, None, None)
assert p._replace(lineno=3).lineno == 3
assert p._asdict()["end_lineno"] == 2
assert dis.Instruction._fields == ("opname", "opcode", "arg", "argval", "argrepr",
                                   "offset", "starts_line", "is_jump_target", "positions")
i = dis.Instruction("TEST", 1, None, None, "", 0, None, False)
assert i.positions is None
assert i[0] == "TEST"
assert i._replace(offset=4).offset == 4
assert i._asdict()["opcode"] == 1
source = "\N{LATIN SMALL LETTER E WITH ACUTE} = 1\nanswer = \N{LATIN SMALL LETTER E WITH ACUTE} + 2\n"
constant = next(ins for ins in dis.get_instructions(compile(source, "<unicode>", "exec"))
                if ins.opname == "LOAD_CONST" and ins.argval == 2)
assert tuple(constant.positions) == (2, 2, 14, 15)
for invalid_jump in (1, "yes", object()):
    try:
        dis.stack_effect(dis.opmap["LOAD_CONST"], 0, jump=invalid_jump)
    except ValueError:
        pass
    else:
        raise AssertionError("invalid jump accepted")
print("dis tuple protocols ok")
