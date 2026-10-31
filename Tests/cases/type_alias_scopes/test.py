value = 'module'
type ModuleAlias = value
print(ModuleAlias.__value__)

def outer():
    value = 'outer'
    class Example:
        type Late = value
        value = 'class'
        __secret = 'private'
        type Private = __secret
        class Nested:
            type Inner = value
    return Example

Example = outer()
print(Example.Late.__value__, Example.Private.__value__)
print(Example.Nested.Inner.__value__)

def scopes():
    local = 'initial'
    def change():
        nonlocal local
        local = 'changed'
        type NonlocalAlias = local
        return NonlocalAlias
    return change()

print(scopes().__value__)

def global_alias():
    global value
    type GlobalAlias = value
    return GlobalAlias

print(global_alias().__value__)

class Invisible:
    type Alias = __classdictcell__

try:
    Invisible.Alias.__value__
except NameError as error:
    print(type(error).__name__, error.name)

def dynamic():
    captured = 'outer cell'
    class Both:
        type Alias = (captured, __class__)
        def cls(self):
            return __class__
    return Both

Both = dynamic()
Both.captured = 'published class'
print(Both.Alias.__value__[0], Both.Alias.__value__[1] is Both, Both().cls() is Both)
print('__classdictcell__' in Both.__dict__, '__classcell__' in Both.__dict__)

def explicit_global():
    value = 'ignored enclosing cell'
    class Global:
        global value
        type Alias = value
    return Global

Global = explicit_global()
Global.value = 'ignored attribute'
print(Global.Alias.__value__)

type type = int
print(type.__name__, type.__value__ is int)

for source in ('type Alias = (yield 1)', 'type Alias = (await value)',
               'type Alias = (x := 1)',
               'class C:\n type Alias = [x for x in values]'):
    try:
        compile(source, '<alias>', 'exec')
    except SyntaxError as error:
        print(error.msg)
