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
