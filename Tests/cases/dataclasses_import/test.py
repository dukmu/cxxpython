import dataclasses

print(dataclasses.is_dataclass(int))
print(dataclasses.MISSING is dataclasses.field().default)
print(dataclasses.FrozenInstanceError.__name__)
