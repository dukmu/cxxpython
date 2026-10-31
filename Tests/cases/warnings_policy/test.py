import io
import linecache
import warnings


def records(label, action):
    registry = {}
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter(action, UserWarning)
        for message, module, lineno in (
            ("repeat", "alpha", 10),
            ("repeat", "alpha", 10),
            ("repeat", "alpha", 11),
            ("repeat", "beta", 12),
            ("other", "alpha", 10),
        ):
            warnings.warn_explicit(message, UserWarning, "<policy>", lineno,
                                   module=module, registry=registry)
        print(label, [(str(w.message), w.lineno) for w in caught])
        print("registry", bool(registry.get(("repeat", UserWarning, 10))))


for action in ("always", "ignore", "default", "once", "module"):
    records(action, action)

with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter("error", UserWarning)
    try:
        warnings.warn_explicit("failure", UserWarning, "<policy>", 20,
                               registry={})
    except UserWarning as exc:
        print("error", type(exc).__name__, str(exc), len(caught))

with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter("ignore")
    warnings.filterwarnings("always", message="^hello", category=UserWarning,
                            module="^pkg\\.sub$", lineno=7)
    for message, module, lineno in (("HELLO there", "pkg.sub", 7),
                                    ("hello", "Pkg.sub", 7),
                                    ("hello", "pkg.sub", 8),
                                    ("goodbye", "pkg.sub", 7)):
        warnings.warn_explicit(message, UserWarning, "<regex>", lineno,
                               module=module, registry={})
    print("regex", [(str(w.message), w.lineno) for w in caught])

with warnings.catch_warnings(record=True) as caught:
    registry = {}
    warnings.simplefilter("default")
    warnings.warn_explicit("versioned", UserWarning, "<registry>", 3,
                           registry=registry)
    warnings.warn_explicit("versioned", UserWarning, "<registry>", 3,
                           registry=registry)
    registry["stale"] = True
    warnings.simplefilter("default")
    warnings.warn_explicit("versioned", UserWarning, "<registry>", 3,
                           registry=registry)
    print("invalidation", len(caught), "stale" in registry,
          ("versioned", UserWarning, 3) in registry)

with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter("always")
    instance = RuntimeWarning("instance")
    warnings.warn_explicit(instance, UserWarning, "<instance>", 9, registry={})
    print("instance", caught[0].message is instance,
          caught[0].category.__name__, str(caught[0].message))

linecache.cache["<format>"] = (100, None, ["  source_call()  \n"], "<format>")
print("format", repr(warnings.formatwarning("message", UserWarning,
                                            "<format>", 1)))
output = io.StringIO()
warnings.showwarning("message", UserWarning, "<format>", 1, file=output,
                     line="  supplied()  ")
print("showwarning", repr(output.getvalue()))

with warnings.catch_warnings():
    seen = []
    def hook(message, category, filename, lineno, file=None, line=None):
        seen.append((str(message), category.__name__, filename, lineno,
                     file is None, line))
    warnings.showwarning = hook
    warnings.simplefilter("always")
    warnings.warn_explicit("hooked", UserWarning, "<hook>", 6, registry={})
    print("hook", seen)

for args in (("bad-action",), ("always", 12),
             ("always", "", str), ("always", "", UserWarning, 12),
             ("always", "", UserWarning, "", -1)):
    try:
        warnings.filterwarnings(*args)
    except (AssertionError, TypeError, ValueError) as exc:
        print("invalid", type(exc).__name__, str(exc))

manager = warnings.catch_warnings(record=True)
with manager:
    try:
        manager.__enter__()
    except RuntimeError as exc:
        print("reenter", type(exc).__name__, "Cannot enter" in str(exc))
try:
    manager.__enter__()
except RuntimeError as exc:
    print("reuse", type(exc).__name__, "Cannot enter" in str(exc))
print("reexit", manager.__exit__(None, None, None))
try:
    warnings.catch_warnings().__exit__(None, None, None)
except RuntimeError as exc:
    print("early exit", type(exc).__name__, "Cannot exit" in str(exc))
