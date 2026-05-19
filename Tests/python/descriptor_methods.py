class Tools:
    def static_value(value):
        return 'static:' + value
    static_value = staticmethod(static_value)

    def class_value(cls, value):
        return cls.__name__ + ':' + value
    class_value = classmethod(class_value)


tools = Tools()
print(Tools.static_value('A'))
print(tools.static_value('B'))
print(Tools.class_value('C'))
print(tools.class_value('D'))
