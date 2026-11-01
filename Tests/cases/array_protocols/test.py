from array import array
import pickle


class NamedArray(array):
    pass


print("repr", repr(array("i")), str(array("i", [1, -2])))
print("unicode", repr(array("u", "A\u03bb")))
print("subclass repr", repr(NamedArray("i", [3])))
print("eq", array("i", [1, 2]) == array("d", [1.0, 2.0]))
print("ne", array("b", [1, 3]) != array("I", [1, 2]))
print("ordering", array("i", [1, 2]) < array("d", [1, 3]),
      array("i", [1, 2]) <= array("q", [1, 2]),
      array("i", [2]) > array("f", [1]),
      array("i", [2]) >= array("B", [2]))
print("prefix", array("i", [1]) < array("i", [1, 0]))
print("not array", array("i", [1]) == [1], array("i", [1]) != [1])
nan = float("nan")
print("nan", array("d", [nan]) == array("d", [nan]),
      array("d", [nan]) < array("d", [1]))
try:
    hash(array("i"))
except TypeError:
    print("unhashable", array.__hash__ is None)

a = array("i", [1])
a.fromlist([2, 3])
try:
    a.fromlist([4, "bad"])
except TypeError:
    print("fromlist rollback", a.tolist())
try:
    a.fromlist((4,))
except TypeError:
    print("fromlist requires list")

constructor, arguments, state = a.__reduce_ex__(2)
print("reduce", constructor is array, arguments, state, constructor(*arguments) == a)
for protocol in range(6):
    restored = pickle.loads(pickle.dumps(a, protocol))
    print("pickle", protocol, type(restored) is array, restored.typecode, restored.tolist())
u = array("u", "AB")
print("pickle unicode", pickle.loads(pickle.dumps(u)).tounicode())
named = NamedArray("i", [8, 9])
named.label = "saved"
restored = pickle.loads(pickle.dumps(named, 2))
print("pickle subtype", type(restored) is NamedArray, restored.tolist(), restored.label)
