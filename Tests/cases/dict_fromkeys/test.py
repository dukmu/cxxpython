# Fromkeys contracts from CPython 3.12.14 Lib/test/test_dict.py (PSF license).
import gc

print(dict.fromkeys([]), dict.fromkeys([1, 2, 1]))
value = []
result = dict.fromkeys(("a", "b"), value)
print(result["a"] is value, result["a"] is result["b"])

class Derived(dict):
    def __setitem__(self, key, value):
        gc.collect()
        super().__setitem__(key, value + 1)

result = Derived.fromkeys(["x", "y"], 4)
print(type(result) is Derived, result)

class Keys:
    def __iter__(self):
        yield "first"
        gc.collect()
        yield "second"

print(dict.fromkeys(Keys(), 7))
for values in (1, [[]]):
    try:
        dict.fromkeys(values)
    except TypeError:
        print("TypeError")

class Broken(dict):
    def __setitem__(self, key, value):
        raise ValueError("write failed")

try:
    Broken.fromkeys([1])
except ValueError as error:
    print(str(error))
