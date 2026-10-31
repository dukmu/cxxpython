import time

class Index:
    def __index__(self):
        return 0

class FloatOnly:
    def __float__(self):
        return 0.0

for value in (0, 0.0, Index(), -1, -0.0000000001, float("nan"),
              float("inf"), 10 ** 100, FloatOnly()):
    try:
        print("result", time.sleep(value))
    except (ValueError, OverflowError, TypeError) as error:
        print(type(error).__name__)
