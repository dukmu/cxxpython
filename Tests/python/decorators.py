def keep(value):
    return value


@keep
def identity(value):
    return value


def decorate_class(cls):
    cls.decorated = 'yes'
    return cls


@decorate_class
class Box:
    label = 'box'

    @property
    def value(self):
        return self.stored

    @value.setter
    def value(self, value):
        self.stored = value

    @staticmethod
    def static_value(value):
        return 'static:' + value

    @classmethod
    def class_value(cls, value):
        return cls.__name__ + ':' + value


box = Box()
box.stored = 'initial'
print(identity('ok'))
print(Box.decorated)
print(box.value)
box.value = 'updated'
print(box.stored)
print(Box.static_value('A'))
print(box.static_value('B'))
print(Box.class_value('C'))
print(box.class_value('D'))
