# CPython 3.12.14 AST constructor, visitor and utility behavior.
import ast
import copy
import gc
import weakref


def show(label, value):
    print(label, repr(value))


show('base fields', (ast.AST._fields, ast.AST._attributes, ast.AST.__match_args__))
show('name fields', (ast.Name._fields, ast.Name._attributes, ast.Name.__match_args__))
show('families', (issubclass(ast.Name, ast.expr), issubclass(ast.FunctionDef, ast.stmt),
                  issubclass(ast.Add, ast.operator), ast.Name.__module__))
node = ast.Name('x', ast.Load(), lineno=3, col_offset=2, extra='custom')
show('constructed', (ast.dump(node, include_attributes=True), node.extra, node.end_lineno))
show('missing', vars(ast.Name()))
show('optional', (ast.Return().value, ast.Slice().lower, ast.Constant().kind))
for factory in (lambda: ast.Name('x', ast.Load(), 3),
                lambda: ast.Name('x', id='again'), lambda: ast.AST(1)):
    try:
        factory()
    except TypeError as error:
        show('constructor error', str(error))
    else:
        raise AssertionError('expected TypeError')


class Custom(ast.AST):
    _fields = ('left', 'right')


custom = Custom(1, right=2, arbitrary=3)
show('custom', (vars(custom), ast.dump(custom)))


class CollectDuringSet(ast.Name):
    def __setattr__(self, key, value):
        gc.collect()
        super().__setattr__(key, value)


show('callback construction', ast.dump(CollectDuringSet('name', ast.Load(), extra=9)))
tree = ast.Expression(ast.BinOp(ast.Constant(2), ast.Add(), ast.Constant(3)))
ast.fix_missing_locations(tree)
show('fields', [(name, type(value).__name__) for name, value in ast.iter_fields(tree.body)])
show('walk', [type(value).__name__ for value in ast.walk(tree)])
show('copy location', ast.dump(ast.copy_location(ast.Name('copy', ast.Load()), node),
                                include_attributes=True))
ast.increment_lineno(tree, 4)
show('locations', ast.dump(tree, include_attributes=True))


class Names(ast.NodeVisitor):
    def __init__(self):
        self.names = []

    def visit_Name(self, node):
        self.names.append(node.id)


class Rename(ast.NodeTransformer):
    def visit_Name(self, node):
        return ast.copy_location(ast.Name(node.id.upper(), node.ctx), node)


parsed = ast.parse('first + second.attr', mode='eval')
visitor = Names()
visitor.visit(parsed)
show('visitor', visitor.names)
show('transform', ast.dump(Rename().visit(parsed)))
source = 'value = (1 + 2) * 3\n'
parsed = ast.parse(source)
show('unparse', ast.unparse(parsed))
show('source segment', ast.get_source_segment(source, parsed.body[0].value))
show('docstring', ast.get_docstring(ast.parse('"""title\n    body"""\nx = 1')))
for literal in ('None', 'True', '123456789012345678901234567890', '-1.25', '3+4j',
                "{'a': [1, 2], 'b': (None, True)}", 'set()', 'b"abc"'):
    show('literal', ast.literal_eval(literal))
for bad in ('name', '1 + 2', '{**{}}', 'f"{x}"'):
    try:
        ast.literal_eval(bad)
    except ValueError:
        show('invalid literal', bad)
    else:
        raise AssertionError('expected ValueError')
clone = copy.deepcopy(parsed)
show('deepcopy', (clone is not parsed, ast.dump(clone) == ast.dump(parsed)))
kind, args, state = node.__reduce__()
show('reduce', (kind is ast.Name, args, state == vars(node)))
cycle = ast.Name('cycle', ast.Load())
cycle.extra = cycle
ref = weakref.ref(cycle)
del cycle
gc.collect()
show('cycle collected', ref() is None)
show('only AST input', compile(parsed, '<tree>', 'exec', ast.PyCF_ONLY_AST) is parsed)
operators = ast.parse('x + y + z', mode='eval').body
show('operator singleton', operators.op is operators.left.op)
changed = ast.Name('unused', ast.Load())
changed.__class__ = ast.Constant
changed.value = 23
expression = ast.fix_missing_locations(ast.Expression(changed))
show('changed class', (ast.dump(changed), eval(compile(expression, '<changed-class>', 'eval'))))
