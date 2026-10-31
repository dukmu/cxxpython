import _thread
import ast
import gc

tree = ast.parse('40 + 2', mode='eval')
gates = [_thread.allocate_lock() for _ in range(5)]
counts = [0] * 3
failures = []


def compiler(index):
    try:
        for iteration in range(12):
            result = eval(compile(tree, '<shared-ast>', 'eval'))
            assert result in (42, 43)
            counts[index] += 1
    except BaseException as error:
        failures.append(type(error).__name__)
    finally:
        gates[index].release()


def writer():
    try:
        for iteration in range(24):
            tree.body.right.value = 2 + iteration % 2
            gc.collect()
    except BaseException as error:
        failures.append(type(error).__name__)
    finally:
        gates[3].release()


def collector():
    try:
        for iteration in range(24):
            gc.collect()
    except BaseException as error:
        failures.append(type(error).__name__)
    finally:
        gates[4].release()


for gate in gates:
    gate.acquire()
for index in range(3):
    _thread.start_new_thread(compiler, (index,))
_thread.start_new_thread(writer, ())
_thread.start_new_thread(collector, ())
for gate in gates:
    gate.acquire()
print(counts, failures)


class CollectingName(ast.Name):
    @property
    def id(self):
        gc.collect()
        return 'value'


node = CollectingName(ctx=ast.Load(), lineno=7, col_offset=4, end_lineno=7, end_col_offset=9)
code = compile(ast.Expression(node), '<property-ast>', 'eval')
print(eval(code, {'value': 17}))
try:
    eval(code)
except NameError as error:
    trace = error.__traceback__
    while trace.tb_next is not None:
        trace = trace.tb_next
    print(trace.tb_lineno, trace.tb_frame.f_code.co_filename)

cycle = ast.UnaryOp(op=ast.Not())
cycle.operand = cycle
ast.copy_location(cycle, node)
try:
    compile(ast.Expression(cycle), '<cyclic-ast>', 'eval')
except RecursionError:
    print('cyclic AST rejected')
else:
    raise AssertionError('expected RecursionError')

unknown_columns = ast.Expression(ast.Constant(value=9, lineno=4, col_offset=-1))
code = compile(unknown_columns, '<unknown-columns>', 'eval')
positions = [position for position in code.co_positions() if position[0] == 4]
print(bool(positions), all(position[2:] == (None, None) for position in positions))


class ExplodingIterator:
    def __iter__(self):
        return self

    def __next__(self):
        raise RuntimeError('iteration failed')


namespace = {'values': ExplodingIterator()}
tree = ast.parse('\n\n\n\nresult = (value for value in values)')
exec(compile(tree, '<comprehension-ast>', 'exec'), namespace)
try:
    next(namespace['result'])
except RuntimeError as error:
    trace = error.__traceback__
    while trace is not None:
        if trace.tb_frame.f_code.co_name == '<genexpr>':
            print('comprehension line', trace.tb_lineno)
            break
        trace = trace.tb_next
    else:
        raise AssertionError('missing generator frame')
