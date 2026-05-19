class Base:
    def name(self):
        return 'base'


class Child(Base):
    def name(self):
        return super().name() + ':child'

    def class_name(self):
        return __class__.__name__


child = Child()
print(child.name())
print(child.class_name())
print(super(Child, child).name())
print(dir(Child).__contains__('__classcell__'))
