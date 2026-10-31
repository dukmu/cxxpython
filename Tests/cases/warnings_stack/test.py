import linecache
import sys
import warnings


def basename(path):
    return path.replace("\\", "/").rsplit("/", 1)[-1]


vendor_source = '''\
import sys
import warnings

def emit(level, prefixes=()):
    frame = sys._getframe()
    own = (frame.f_code.co_filename, frame.f_lineno + 2, frame.f_code.co_name)
    caller = (frame.f_back.f_code.co_filename, frame.f_back.f_lineno, frame.f_back.f_code.co_name)
    warnings.warn("stack", UserWarning, stacklevel=level, skip_file_prefixes=prefixes)
    return own, caller

def relay(level, prefixes=()):
    frame = sys._getframe()
    caller = (frame.f_back.f_code.co_filename, frame.f_back.f_lineno, frame.f_back.f_code.co_name)
    return emit(level, prefixes), caller
'''
app_source = '''\
import sys

def call(level, prefixes=()):
    frame = sys._getframe()
    expected = (frame.f_code.co_filename, frame.f_lineno + 1, frame.f_code.co_name)
    result, relay_caller = relay(level, prefixes)
    return result, relay_caller, expected
'''
vendor_file = "/virtual/vendor/wrapper.py"
app_file = "/virtual/app/main.py"
linecache.cache[vendor_file] = (len(vendor_source), None,
                                vendor_source.splitlines(True), vendor_file)
linecache.cache[app_file] = (len(app_source), None,
                             app_source.splitlines(True), app_file)
namespace = {}
exec(compile(vendor_source, vendor_file, "exec"), namespace)
exec(compile(app_source, app_file, "exec"), namespace)

for label, level, prefixes in (("level1", 1, ()),
                               ("level2", 2, ()),
                               ("level3", 3, ()),
                               ("skip", 1, ("/virtual/vendor/",)),
                               ("skip-no-match", 1, ("/other/",))):
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        (own, caller), relay_caller, expected = namespace["call"](level, prefixes)
        warning = caught[0]
        target = own if level == 1 and not prefixes else caller
        if level == 3 or label == "skip":
            target = expected
        print(label, basename(warning.filename), warning.lineno,
              warning.filename == target[0], warning.lineno == target[1],
              target[2], relay_caller == expected)
