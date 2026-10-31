# VM-specific: offsets are actual instruction indexes, not CPython wordcode bytes.
import _opcode
import dis
import gc
import sys
import traceback


def sample(value):
    if value:
        return value + 1
    return 0


def check(code):
    rows = _opcode.instructions(code)
    expected = [tuple(row[3:7]) if row[3] else (None, None, None, None) for row in rows]
    positions = code.co_positions()
    assert iter(positions) is positions
    assert list(positions) == expected
    assert list(code.co_positions()) == expected
    assert [tuple(instruction.positions) for instruction in dis.get_instructions(code)] == expected
    lines = code.co_lines()
    assert iter(lines) is lines
    expanded = []
    end = 0
    previous = object()
    for start, stop, line in lines:
        assert start == end and start < stop <= len(rows)
        assert line != previous
        expanded.extend([line] * (stop - start))
        end = stop
        previous = line
    assert end == len(rows)
    assert expanded == [position[0] for position in expected]
    for offset in range(len(rows)):
        assert traceback._get_code_position(code, offset) == expected[offset]
    assert traceback._get_code_position(code, -1) == (None, None, None, None)
    return rows, expected


check(sample.__code__)
print('positions and line ranges match actual VM metadata')

source = 'def accented():\n    return "\u00e9" + missing\n'
namespace = {}
exec(compile(source, '<code-positions>', 'exec'), namespace)
code = namespace['accented'].__code__
rows, expected = check(code)
line = source.splitlines()[1]
column = len(line[:line.index('missing')].encode('utf-8'))
loads = [instruction for instruction in dis.get_instructions(code)
         if instruction.opname == 'LOAD_GLOBAL' and instruction.argval == 'missing']
assert len(loads) == 1
assert loads[0].positions == (2, 2, column, column + len('missing'))
try:
    namespace['accented']()
except NameError as exc:
    tb = exc.__traceback__
    while tb.tb_next is not None:
        tb = tb.tb_next
    assert tb.tb_frame.f_code is code
    assert tb.tb_lineno == 2
    assert expected[tb.tb_lasti] == tuple(loads[0].positions)
    assert traceback._get_code_position(code, tb.tb_lasti) == expected[tb.tb_lasti]
else:
    raise AssertionError('expected NameError')
print('UTF-8 byte columns and traceback instruction index match')


def active():
    frame = sys._getframe()
    return frame, frame.f_lasti


frame, lasti = active()
instructions = list(dis.get_instructions(active))
assert instructions[lasti].opname == 'LOAD_ATTR'
assert instructions[lasti].argval == 'f_lasti'
assert instructions[frame.f_lasti].opname == 'RETURN'
for owner, attribute in ((frame, 'f_lasti'), (frame, 'f_lineno'),
                         (code, 'co_firstlineno'), (code, 'co_positions')):
    try:
        setattr(owner, attribute, 0)
    except (AttributeError, TypeError):
        pass
    else:
        raise AssertionError(attribute + ' must be readonly')
print('frame instruction position and metadata are readonly')


def snapshots():
    temporary = compile('x = 1\nx = x + 1\n', '<temporary-positions>', 'exec')
    rows, expected = check(temporary)
    return temporary.co_positions(), temporary.co_lines(), expected


positions, lines, expected = snapshots()
gc.collect()
assert list(positions) == expected
assert sum(stop - start for start, stop, line in lines) == len(expected)
print('position snapshots survive Code collection')
