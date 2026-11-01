import gc
import pickle
import types

values = []
bound = values.append
print("native types", type(bound) is types.BuiltinFunctionType,
      types.BuiltinMethodType is types.BuiltinFunctionType,
      type(dict.fromkeys) is types.BuiltinFunctionType)
print("receiver", bound.__self__ is values, dict.fromkeys.__self__ is dict)
print("not Python method", not isinstance(bound, types.MethodType),
      not hasattr(bound, "__func__"))
print("equality", bound == values.append, hash(bound) == hash(values.append),
      bound != [].append, bound == len)
gc.collect()
bound(3)
print("called", values, sorted(dict.fromkeys(["a", "b"], 7).items()))
types.MethodType(bound, 4)()
print("explicit method", values)
for protocol in range(6):
    clone = pickle.loads(pickle.dumps(dict.fromkeys, protocol))
    print("pickle", protocol, clone.__self__ is dict, clone(["a"], 7))


class Example:
    def method(self):
        return 1


method = Example().method
print("Python method", type(method) is types.MethodType, hasattr(method, "__func__"))
