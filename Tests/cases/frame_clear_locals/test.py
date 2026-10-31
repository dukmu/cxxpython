import gc
import sys
import weakref


class Item:
    pass


def finished():
    item = Item()
    ref = weakref.ref(item)
    return sys._getframe(), ref


frame, ref = finished()
gc.collect()
print('finished retained', ref() is not None)
print('clear returns', frame.clear())
gc.collect()
print('finished released', ref() is None)
print('clear again', frame.clear())


def executing():
    try:
        sys._getframe().clear()
    except RuntimeError as exc:
        print('executing', type(exc).__name__, str(exc))


executing()


def make_closure():
    item = Item()
    ref = weakref.ref(item)

    def get_item():
        return item

    return sys._getframe(), get_item, ref


frame, get_item, ref = make_closure()
frame.clear()
gc.collect()
print('closure intact', ref() is not None, get_item() is ref())
del get_item
gc.collect()
print('closure released', ref() is None)
