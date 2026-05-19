class A:
    def get_a(self):
        return self.saved
    def set_a(self, value):
        self.saved = value
    a = property(get_a, set_a)

a = A()
a.a = 'stored'
print(a.a)
