import readline

def complete(text, state):
    if text == 'al' and state == 0:
        return 'alpha'
    return None

def before_input():
    print('HOOK')
    readline.insert_text('seed')

readline.parse_and_bind('tab: complete')
readline.parse_and_bind('"\\e[A": previous-history')
readline.set_completer(complete)
readline.set_pre_input_hook(before_input)
print('RESULT', input('prompt> '))
readline.set_pre_input_hook(None)
print('HISTORY', input('again> '))
