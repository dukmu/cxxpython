class Dynamic:
    def __getattr__(self, name):
        return 'dynamic:' + name


item = Dynamic()
print(item.missing)
item.present = 'stored'
print(item.present)
print(item.other)
