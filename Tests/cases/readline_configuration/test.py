import readline

def complete(text, state):
    return None

readline.set_completer(complete)
print(readline.get_completer() is complete)
readline.set_completer(None)
print(readline.get_completer() is None)
old = readline.get_completer_delims()
readline.set_completer_delims(' /')
print(readline.get_completer_delims())
readline.set_completer_delims(old)
readline.parse_and_bind('tab: complete')
print(type(readline.get_line_buffer()).__name__)
readline.set_startup_hook(None)
readline.set_pre_input_hook(None)
