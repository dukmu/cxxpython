import sys

_frame_type = type(sys._getframe())
_code_type = type(sys._getframe().f_code)

def currentframe():
    return sys._getframe(1)

def isframe(object):
    return type(object) is _frame_type

def iscode(object):
    return type(object) is _code_type

def isfunction(object):
    return type(object).__name__ == 'function'

def getsourcefile(object):
    if isframe(object):
        return object.f_code.co_filename
    if iscode(object):
        return object.co_filename
    return object.__code__.co_filename

def getfile(object):
    return getsourcefile(object)
