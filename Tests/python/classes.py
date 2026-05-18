class A:
    label = 'class-label'

    def get_label(self):
        return self.label


class B(A):
    extra = 'B.extra'


a = A()
b = B()
print(A.__name__)
print(A.__module__)
print(A.label)
print(a.label)
print(a.get_label())
print(type(a))
print(type(A))
print(B.__bases__)
print(b.label)
print(b.extra)
print(type(b))
