import curses


def application(window):
    assert curses.error is not RuntimeError
    height, width = window.getmaxyx()
    assert height > 0 and width > 0
    assert curses.LINES == height and curses.COLS == width
    child = curses.newwin(2, 12, 1, 1)
    assert child.getmaxyx() == (2, 12)
    assert child.getbegyx() == (1, 1)
    assert isinstance(child, curses.window)
    assert curses.tigetnum("cols") > 0
    assert isinstance(curses.tigetstr("clear"), bytes)
    try:
        window.move(-1, -1)
    except curses.error:
        pass
    else:
        raise AssertionError("invalid move did not raise curses.error")
    window.erase()
    window.move(0, 0)
    window.addstr("curses-pty")
    assert window.getyx() == (0, 10)
    window.refresh()
    window.addstr(1, 0, "\u4e2d\u00e9", curses.A_BOLD)
    assert window.getyx() == (1, 3)
    window.addstr(2, 0, b"bytes", curses.A_REVERSE)
    assert window.getyx() == (2, 5)
    for value in ("bad\0text", b"bad\0text"):
        try:
            window.addstr(value)
        except ValueError:
            pass
        else:
            raise AssertionError('embedded null accepted')
    window.attron(curses.A_BOLD)
    window.attroff(curses.A_BOLD)
    window.attrset(curses.A_NORMAL)
    window.clrtoeol()
    window.clrtobot()
    window.touchwin()
    window.noutrefresh()
    curses.ungetch(ord('q'))
    assert window.getch(3, 0) == ord('q')
    window.nodelay(True)
    assert window.getch() == -1
    window.nodelay(False)
    curses.ungetch(ord('\n'))
    for ch in reversed('abc'):
        curses.ungetch(ord(ch))
    assert window.getstr(3, 0, 3) == b'abc'
    curses.unget_wch('\u00e9')
    assert window.get_wch() == '\u00e9'
    window.timeout(0)
    try:
        window.get_wch()
    except curses.error:
        pass
    else:
        raise AssertionError('nonblocking wide input did not fail')
    window.timeout(-1)
    if curses.has_colors():
        curses.init_pair(1, curses.COLOR_RED, curses.COLOR_BLACK)
        assert curses.pair_number(curses.color_pair(1)) == 1
    raise ValueError("expected")


try:
    curses.wrapper(application)
except ValueError:
    print("CURSES_RESTORED")

def again(window):
    window.addstr(0, 0, 'second session')
    window.refresh()
    return 42

assert curses.wrapper(again) == 42
