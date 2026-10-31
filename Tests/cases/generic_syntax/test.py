cases = (
    ("duplicate function", "def f[T, T](): pass"),
    ("duplicate alias", "type A[T, T] = T"),
    ("private collision", "class C:\n def f[__T, _C__T](): pass"),
    ("tuple bound", "def f[*Ts: int](): pass"),
    ("paramspec bound", "def f[**P: int](): pass"),
    ("nonlocal parameter", "def outer():\n def f[T]():\n  nonlocal T"),
    ("alias walrus", "type A = (x := 1)"),
    ("alias yield", "type A = (yield 1)"),
    ("alias await", "type A = await f()"),
    ("bound walrus", "def f[T: (x := 1)](): pass"),
    ("bound yield", "def f[T: (yield 1)](): pass"),
    ("bound await", "def f[T: await g()](): pass"),
    ("constraints walrus", "def f[T: (int, (x := str))](): pass"),
    ("alias lambda walrus", "type A = lambda: (x := 1)"),
    ("alias lambda yield", "type A = lambda: (yield 1)"),
    ("bound lambda walrus", "def f[T: (lambda: (x := 1))](): pass"),
    ("bound lambda yield", "def f[T: (lambda: (yield 1))](): pass"),
)

for label, source in cases:
    try:
        compile(source, "<test>", "exec")
    except SyntaxError as exc:
        print(label, type(exc).__name__)
    else:
        print(label, "ok")
