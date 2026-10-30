def abstract(f):
    f.__isabstractmethod__ = True
    return f

def plain():
    pass

def marked():
    pass

abstract(marked)
print(classmethod(plain).__isabstractmethod__)
print(staticmethod(plain).__isabstractmethod__)
print(classmethod(marked).__isabstractmethod__)
print(staticmethod(marked).__isabstractmethod__)
print(property(marked).__isabstractmethod__)
