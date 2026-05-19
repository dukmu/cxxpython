Meta = type('Meta', (type,), {})
Base = Meta('Base', (), {'base': 'base'})


class Child(Base):
    value = 'child'


obj = Child()
print(type(Meta))
print(type(Base))
print(type(Child))
print(Child.__bases__)
print(obj.base)
print(obj.value)
events = []


class PreparedMeta(type):
    @classmethod
    def __prepare__(mcls, name, bases):
        events.append('prepare:' + name)
        return {'prepared': 'yes'}


class PreparedClass(metaclass=PreparedMeta):
    marker = prepared


print(events)
print(PreparedClass.marker)
print(type(PreparedClass))
print(dir(__builtins__).__contains__('__build_class__'))
