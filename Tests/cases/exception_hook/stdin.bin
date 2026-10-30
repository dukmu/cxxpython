import sys
def hook(kind, value, traceback):
    print(kind.__name__, str(value), traceback is value.__traceback__)
sys.excepthook = hook
raise ValueError('entrypoint')
