class A:
    def get_a(self):
        return 'A.a'
    a = property(get_a)

print(A.a)
a = A()
print(a.a)
print(type(a.a))
a.a = 2
