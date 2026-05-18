class A:
    label = 'class-label'

    def get_label(self):
        return self.label


a = A()
print(A.__name__)
print(A.__module__)
print(A.label)
print(a.label)
print(a.get_label())
print(type(a))
print(type(A))
