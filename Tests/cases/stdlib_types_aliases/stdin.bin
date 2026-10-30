# Adapted from CPython 3.12.14 Lib/test/test_types.py TypesTests.
import types

assert isinstance(object.__init__, types.WrapperDescriptorType)
assert isinstance(object().__str__, types.MethodWrapperType)
assert isinstance(str.join, types.MethodDescriptorType)
assert isinstance([].append, types.BuiltinMethodType)
assert isinstance(dict.__dict__['fromkeys'], types.ClassMethodDescriptorType)
assert isinstance(None, types.NoneType)
assert isinstance(Ellipsis, types.EllipsisType)
assert isinstance(NotImplemented, types.NotImplementedType)
try:
    raise OSError
except OSError as exc:
    assert isinstance(exc.__traceback__, types.TracebackType)
    assert isinstance(exc.__traceback__.tb_frame, types.FrameType)
print('types aliases ok')
