sources = [
    'match x:\n case [a, a]: pass',
    'match x:\n case [a] | [b]: pass',
    'match x:\n case name: pass\n case 1: pass',
    'match x:\n case _ | 1: pass',
    'match x:\n case {0: _, False: _}: pass',
    'match x:\n case C(a=_, a=_): pass',
    'match x:\n case [*a, *b]: pass',
    'match x:\n case [a] as a: pass',
    'match x:\n case 1 + 2: pass',
    'match x:\n case 1j + 2j: pass',
    'match x:\n case {1 - 2: _}: pass',
    'match x:\n case f"text": pass',
    'match x:\n case {f"text": _}: pass',
    'class C:\n match x:\n  case (__x, _) | (_, _C__x): pass',
    'match x:\n case {1: _, 1.0: _}: pass',
    'match x:\n case {1: _, 1 + 0j: _}: pass',
    'match x:\n case {-0.0: _, False: _}: pass',
    'match x:\n case {0xF: _, 15: _}: pass',
    'match x:\n case {"ab": _, "a" "b": _}: pass',
    'match x:\n case {b"x": _, b"x": _}: pass',
    'match x:\n case {None: _, None: _}: pass',
]
for source in sources:
    try:
        compile(source, '<pattern>', 'exec')
    except SyntaxError:
        print('syntax error')
    else:
        print('unexpected success')
