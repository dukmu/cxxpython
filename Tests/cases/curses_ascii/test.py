import curses
from curses import ascii

print(ascii.isalpha('a'), ascii.isalpha('9'), ascii.isdigit('9'))
print(ascii.isctrl(1), ascii.isprint('~'), ascii.isspace('\n'))
print(ascii.ctrl('a'), ascii.alt('a'), ascii.ascii(255))
print(ascii.unctrl(1), ascii.unctrl(127))
print(issubclass(curses.error, Exception), curses.error is RuntimeError)
for operation in (curses.endwin, curses.newwin, curses.isendwin, curses.curs_set):
    try:
        if operation is curses.newwin:
            operation(2, 2)
        elif operation is curses.curs_set:
            operation(1)
        else:
            operation()
    except curses.error:
        print('not initialized')
    else:
        print('unexpected success')
