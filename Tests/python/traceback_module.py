import traceback


class Writer:
    def __init__(self):
        self.text = ''

    def write(self, text):
        self.text = self.text + text

    def flush(self):
        pass


def fail():
    len(1)


try:
    fail()
except TypeError as exc:
    lines = traceback.format_exception(exc)
    text = ''.join(lines)
    print(lines[0].__contains__('Traceback'))
    print(text.__contains__('traceback_module.py'))
    print(text.__contains__('fail'))
    print(text.__contains__('TypeError'))
    print(traceback.format_tb(exc.__traceback__)[0].__contains__('File'))
    writer = Writer()
    traceback.print_exception(exc, writer)
    print(writer.text == text)
    filtered = [exc.__traceback__[0]]
    filtered_lines = traceback.format_exception(exc, filtered)
    print(len(filtered_lines) < len(lines))
