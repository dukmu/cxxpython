# Adapted from CPython 3.12.14 Lib/test/test_runpy.py module execution cases.
import os
import runpy
import sys
import types

directory = "runpy_source"
os.mkdir(directory)
os.mkdir(directory + "/package")
files = {
    "module.py": (
        "import sys\n"
        "answer = seed + 2\n"
        "metadata = (__name__, __package__, __spec__.name)\n"
        "temporary = sys.modules.get(__name__)\n"
        "registered = temporary is not None and temporary.__dict__ is globals()\n"
        "path_matches = sys.argv[0] == __file__\n"
    ),
    "broken.py": (
        "import sys\n"
        "events.append(sys.modules[__name__].__dict__ is globals())\n"
        "raise ValueError('body failure')\n"
    ),
    "package/__init__.py": "initialized = True\n",
    "package/helper.py": "answer = 73\n",
    "package/__main__.py": (
        "from .helper import answer\n"
        "metadata = (__name__, __package__, __spec__.name)\n"
    ),
}
for name, source in files.items():
    with open(directory + "/" + name, "w", encoding="utf-8") as stream:
        stream.write(source)
saved_argv = sys.argv[:]
sys.path.insert(0, directory)
previous = types.ModuleType("temporary_run")
sys.modules["temporary_run"] = previous
initial = {"seed": 40, "__name__": "overwritten"}
try:
    result = runpy.run_module("module", initial)
    print("plain", result["answer"], result["metadata"], result["registered"])
    assert "module" not in sys.modules
    assert sys.argv == saved_argv
    assert initial == {"seed": 40, "__name__": "overwritten"}
    result = runpy.run_module("module", initial, "temporary_run", True)
    print("altered", result["answer"], result["metadata"], result["registered"],
          result["path_matches"])
    assert sys.modules["temporary_run"] is previous
    assert result is not result["temporary"].__dict__
    assert sys.argv == saved_argv
    result = runpy.run_module("package", run_name="named.package")
    print("package", result["answer"], result["metadata"])
    assert "package.__main__" not in sys.modules
    events = []
    try:
        runpy.run_module("broken", {"events": events}, "temporary_run", True)
    except ValueError as error:
        print("error", str(error), events)
    else:
        raise AssertionError("body exception swallowed")
    assert sys.modules["temporary_run"] is previous
    assert sys.argv == saved_argv
    for name in (".relative", "missing_run_module"):
        try:
            runpy.run_module(name)
        except ImportError:
            print("lookup rejected")
        else:
            raise AssertionError(name)
finally:
    sys.path.pop(0)
    del sys.modules["temporary_run"]
    for name in files:
        os.unlink(directory + "/" + name)
    # CPython writes bytecode for imported package files; the product does not.
    for folder in (directory + "/package", directory):
        cache = folder + "/__pycache__"
        if os.path.isdir(cache):
            for name in os.listdir(cache):
                os.unlink(cache + "/" + name)
            os.rmdir(cache)
        os.rmdir(folder)
