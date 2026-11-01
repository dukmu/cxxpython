import io
import pickle
import pickletools

data = b'\x80\x02K*.'
print([(op.name, arg, pos) for op, arg, pos in pickletools.genops(data)])
output = io.StringIO()
pickletools.dis(data, out=output)
print(output.getvalue(), end='')

value = ['shared', 42, None]
data = pickle.dumps(value, protocol=4)
print(pickle.loads(pickletools.optimize(data)) == value)
