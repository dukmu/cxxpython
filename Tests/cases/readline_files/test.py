import os
import readline

filename = "readline_history.txt"
readline.clear_history()
try:
    for value in ("one", "two", "three"):
        readline.add_history(value)
    readline.set_history_length(2)
    readline.write_history_file(filename)
    readline.clear_history()
    readline.read_history_file(filename)
    print("truncated", [readline.get_history_item(i) for i in range(1, 3)])
    readline.clear_history()
    readline.add_history("four")
    readline.append_history_file(1, filename)
    readline.clear_history()
    readline.read_history_file(filename)
    print("appended", [readline.get_history_item(i) for i in range(1, 3)])

    class Path:
        def __fspath__(self):
            return filename

    for path in (filename.encode(), Path()):
        readline.clear_history()
        readline.read_history_file(path)
        assert readline.get_current_history_length() == 2
    print("filesystem paths")
    try:
        readline.append_history_file(-1, filename)
    except ValueError as error:
        print("negative count", str(error))
    else:
        raise AssertionError("negative append count accepted")
    try:
        readline.read_history_file("absent_directory/history")
    except OSError as error:
        assert error.errno == 2
        print("file error")
    else:
        raise AssertionError("missing file accepted")
finally:
    readline.clear_history()
    readline.set_history_length(-1)
    os.unlink(filename)
