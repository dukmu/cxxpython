for source in (
    "async def f():\n yield from []\n",
    "async def f():\n yield 1\n return 2\n",
    "async def f():\n return 2\n yield 1\n",
    "def f():\n await f()\n",
):
    try:
        compile(source, "<asyncgen syntax>", "exec")
    except SyntaxError:
        print("SyntaxError")

async def f():
    yield 1
    return

g = f()
try:
    iter(g)
except TypeError:
    print("not a synchronous iterator")
try:
    g.aclose().send(None)
except StopIteration:
    print("closed")
