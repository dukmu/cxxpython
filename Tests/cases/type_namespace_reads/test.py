import gc


events = []


class Name(str):
    def __hash__(self):
        events.append("hash")
        return str.__hash__(self)

    def __eq__(self, other):
        events.append("eq")
        gc.collect()
        return str.__eq__(self, other)


class Plain:
    field = "class"


plain = Plain()
print(getattr(plain, Name("field")), "hash" in events, "eq" in events)
plain.field = "instance"
print(getattr(plain, "field"), getattr(Plain, "field"))
del plain.field
print(getattr(plain, "field"))
Plain.field = "changed"
gc.collect()
print(plain.field)
del Plain.field
print(hasattr(plain, "field"))


class BadName(str):
    def __hash__(self):
        raise ValueError("name hash failed")


try:
    getattr(plain, BadName("field"))
except ValueError as error:
    print(type(error).__name__, str(error))


class Alias:
    def __hash__(self):
        return hash("field")

    def __eq__(self, other):
        if armed:
            target.marker = "published during equality"
            gc.collect()
        return other == "field"


armed = False
target = type("Mixed", (), {Alias(): "alias value"})
armed = True
print(getattr(target(), "field"), target.marker)


class MissingName(str):
    def __hash__(self):
        return hash("field")


print(getattr(target(), MissingName("absent"), "fallback"), target.marker)


action = None


class ChangingKey:
    def __hash__(self):
        return hash("field")

    def __eq__(self, other):
        global action
        pending = action
        action = None
        if pending is not None:
            pending()
            gc.collect()
        return other == "field"


changed = type("Changing", (), {ChangingKey(): "old"})
action = lambda: setattr(changed, "field", "updated during equality")
print(changed.field)
# Force a cold CPython lookup so the next read actually invokes __eq__.
changed.marker = "invalidate attribute cache"
action = lambda: delattr(changed, "field")
print(getattr(changed, "field", "removed during equality"))


def replace_key():
    del replaced.field
    replaced.field = "new key"


replaced = type("Replacing", (), {ChangingKey(): "old key"})
action = replace_key
print(replaced.field)


class Naming:
    def __set_name__(self, owner, name):
        owner.saved = owner.second
        del owner.second
        gc.collect()
        owner.first = "replacement"


class SetName:
    first = Naming()
    second = object()


print(SetName.first, type(SetName.saved).__name__, hasattr(SetName, "second"))
