def check(label, source):
    try:
        compile(source, "<case>", "exec")
    except SyntaxError as exc:
        print(label, "SyntaxError", exc.msg)
    else:
        print(label, "ok")


for statement in ("return", "break", "continue"):
    check(statement, "def f():\n while True:\n  try:\n   pass\n  except* ValueError:\n   " + statement + "\n")
check("nested loop", "try:\n pass\nexcept* ValueError:\n for x in []:\n  break\n  continue\n")
check("nested function", "try:\n pass\nexcept* ValueError:\n def f():\n  return 1\n")
check("mixed", "try:\n pass\nexcept ValueError:\n pass\nexcept* TypeError:\n pass\n")
check("mixed reverse", "try:\n pass\nexcept* ValueError:\n pass\nexcept TypeError:\n pass\n")

for label, matcher in (("integer", 42), ("object class", object), ("group class", ExceptionGroup), ("group tuple", (ValueError, BaseExceptionGroup))):
    try:
        try:
            raise ExceptionGroup("root", [ValueError("v")])
        except* matcher:
            print("unexpected match")
    except TypeError as exc:
        print(label, type(exc).__name__, str(exc))
