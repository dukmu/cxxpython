import gc, sys, _weakref
gc.disable()
events = []
def bad(ref): raise ValueError('callback')
def hook(args):
    rebuilt = type(args)(tuple(args))
    events.append((isinstance(args, tuple), len(args), args.object is bad,
                   str(args.exc_value), rebuilt == args, args.err_msg))
    print(args.exc_type is ValueError, args.exc_traceback is args.exc_value.__traceback__,
          args[-1] is bad, type(args).n_fields, type(args).n_sequence_fields,
          type(args).n_unnamed_fields)
    try:
        args.exc_value = None
    except AttributeError as error:
        print(type(error).__name__)
    try:
        args[5]
    except IndexError as error:
        print(type(error).__name__)
sys.unraisablehook = hook
class C: pass
obj = C()
obj.cycle = obj
ref = _weakref.ref(obj, bad)
del obj
gc.collect()
print(events, ref.__callback__ is bad, ref() is None)
