def check_error(call, expected):
    try:
        call()
    except TypeError as error:
        assert str(error) == expected
        print(error)
    else:
        raise AssertionError("expected TypeError")


check_error(
    lambda: str.format(42, "value"),
    "descriptor 'format' for 'str' objects doesn't apply to a 'int' object",
)
check_error(
    lambda: dict.get([], "key"),
    "descriptor 'get' for 'dict' objects doesn't apply to a 'list' object",
)
check_error(
    lambda: list.sort.__get__({}, dict),
    "descriptor 'sort' for 'list' objects doesn't apply to a 'dict' object",
)
check_error(lambda: str.format(), "unbound method str.format() needs an argument")

print(str.format.__get__(None, str) is str.format)
print(dict.get.__get__(None, dict) is dict.get)
print(list.sort.__get__(None, list) is list.sort)
assert str.format.__get__(None, str) is str.format
assert dict.get.__get__(None, dict) is dict.get
assert list.sort.__get__(None, list) is list.sort
