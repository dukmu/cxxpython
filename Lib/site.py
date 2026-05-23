"""Site-specific initialization helpers.

This small subset follows CPython's layering for interactive exit helpers:
``exit`` and ``quit`` are installed by ``site`` rather than being native
builtins.
"""

import builtins

__all__ = ['Quitter']


class Quitter:
    """Object that raises SystemExit when called."""

    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return 'Use ' + self.name + '() or Ctrl-D to exit'

    def __call__(self, code=None):
        raise SystemExit(code)


builtins.exit = Quitter('exit')
builtins.quit = Quitter('quit')
