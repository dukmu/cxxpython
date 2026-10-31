# The current verified VM requires positive source lines, unlike CPython ASTs.
import ast

for line in (0, -1):
    tree = ast.Expression(ast.Constant(value=1, lineno=line, col_offset=0))
    try:
        compile(tree, '<line-limit>', 'eval')
    except ValueError as error:
        assert str(error) == 'AST line numbers must be positive in this VM'
    else:
        raise AssertionError('expected the explicit VM source-position limit')
print('non-positive AST lines report the VM limit as a Python exception')
