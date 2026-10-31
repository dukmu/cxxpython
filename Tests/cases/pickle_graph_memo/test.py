import io
import pickle

shared = ['shared']
graph = [shared, shared]
graph.append(graph)
graph.append({'owner': graph, 'child': shared})
for protocol in (1, 2, 3, 4, 5):
    result = pickle.loads(pickle.dumps(graph, protocol=protocol))
    assert result[0] is result[1] is result[3]['child']
    assert result[2] is result and result[3]['owner'] is result
    result[0].append('changed')
    assert result[1] == ['shared', 'changed'] and shared == ['shared']
    print('graph', protocol, True)

stream = io.BytesIO()
pickler = pickle.Pickler(stream, protocol=4)
pickler.dump(shared)
pickler.dump(shared)
pickler.clear_memo()
pickler.dump(shared)
stream.seek(0)
unpickler = pickle.Unpickler(stream)
first, second, third = unpickler.load(), unpickler.load(), unpickler.load()
assert first is second and first is not third and first == third == shared
print('memo reuse and clear', True)

# The legacy module mapping is part of public fix_imports semantics.
assert pickle.loads(b'c__builtin__\nset\n.') is set
assert pickle.loads(b'c__builtin__\nxrange\n.') is range
print('legacy globals', True)

result = pickle.loads(pickle.dumps(graph, protocol=0))
assert result[0] is result[1] is result[3]['child']
assert result[2] is result and result[3]['owner'] is result
print('graph', 0, True)
