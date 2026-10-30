import readline

readline.clear_history()
print(readline.get_current_history_length())
readline.add_history('alpha')
readline.add_history('beta')
readline.add_history('gamma')
print(readline.get_current_history_length())
print([readline.get_history_item(i) for i in range(5)])
readline.replace_history_item(1, 'changed')
readline.remove_history_item(0)
print([readline.get_history_item(i) for i in range(1, 4)])
for operation in (readline.remove_history_item,):
    try:
        operation(10)
    except Exception as error:
        print(type(error).__name__)
try:
    readline.replace_history_item(10, 'missing')
except Exception as error:
    print(type(error).__name__)
readline.set_history_length(7)
print(readline.get_history_length())
readline.clear_history()
print(readline.get_current_history_length())
