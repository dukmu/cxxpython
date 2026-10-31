import dataclasses
import io
import pprint

# CPython 3.12.14 test_pprint checks recursive containers, dataclass fields,
# sorted dictionaries, depth limits, compact output and stream printing.
@dataclasses.dataclass
class Record:
    name: str
    numbers: list
    hidden: int = dataclasses.field(default=42, repr=False)


record = Record('alpha', [1, 2, 3, 4])
print('dataclass')
print(pprint.pformat(record, width=20))
print('sorted')
print(pprint.pformat({'z': [3, 4], 'a': [1, 2]}, width=14))
print('depth', pprint.pformat({'a': [[1, 2]]}, depth=2))
print('compact')
print(pprint.pformat(list(range(10)), width=15, compact=True))
print('numbers', pprint.pformat([1000000, 123456789], underscore_numbers=True))
stream = io.StringIO()
pprint.pprint({'b': 2, 'a': 1}, stream=stream)
print('stream', stream.getvalue() == "{'a': 1, 'b': 2}\n")

recursive = []
recursive.append(recursive)
representation = pprint.saferepr(recursive)
print('recursive', pprint.isrecursive(recursive), pprint.isreadable(recursive),
      representation.startswith('[<Recursion on list with id='), representation.endswith('>]'))
plain = {'items': [1, 2]}
print('plain', pprint.isrecursive(plain), pprint.isreadable(plain), pprint.saferepr(plain))
