import gc
from types import MethodType

class Receiver:
    __hash__ = None
    def __eq__(self, other):
        raise AssertionError('method receivers compare by identity')
    def method(self):
        return 3
first = Receiver()
second = Receiver()
print(first.method == first.method, first.method != first.method)
print(first.method == second.method, first.method == 3)
print(hash(first.method) == hash(first.method))
values = []
print(values.append == values.append, hash(values.append) == hash(values.append))

class Function:
    def __init__(self, value):
        self.value = value
    def __call__(self, instance):
        return self.value
    def __eq__(self, other):
        gc.collect()
        return self.value == other.value
    def __hash__(self):
        gc.collect()
        return self.value
left = MethodType(Function(7), first)
right = MethodType(Function(7), first)
print(left == right, left != right, hash(left) == hash(right), left())
