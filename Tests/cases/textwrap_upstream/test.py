# Samples adapted from CPython 3.12.14 Lib/test/test_textwrap.py.
# Original tests by Greg Ward; PyUnit conversion by Peter Hansen.
# PSF license: doc/licenses/python-3.12.14.txt. Lib/textwrap.py is copied unchanged.
import gc
from textwrap import TextWrapper, wrap, fill, dedent, indent, shorten


def show(label, result):
    # repr preserves significant whitespace without trailing whitespace in goldens.
    print(label, repr(result))


def error(label, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except ValueError as exc:
        show(label, (type(exc).__name__, str(exc)))
    else:
        raise AssertionError(label)


# WrapTestCase: basic wrapping, whitespace, sentence endings and hyphens.
text = "Hello there, how are you this fine day?  I'm glad to hear it!"
for width in (12, 42, 80):
    show('simple ' + str(width), wrap(text, width))
show('default width', wrap(text))
for options in ({}, {'drop_whitespace': False}, {'initial_indent': '++'}):
    show('empty', wrap('', 6, **options))
show('empty fill', fill(''))
show('tabs default', wrap('\tTest\tdefault\t\ttabsize.', 80))
show('tabs custom', wrap('\tTest\tcustom\t\ttabsize.', 80, tabsize=4))
show('tabs not expanded', wrap('\tTest\tdefault\t\ttabsize.', 80, expand_tabs=False))
show('whitespace retained', wrap('a\tb\nc', 80,
                                 expand_tabs=False, replace_whitespace=False))

paragraph = (
    'This is a paragraph that already has\n'
    'line breaks.  But some of its lines are much longer than the others,\n'
    'so it needs to be wrapped.\n'
    'Some lines are \ttabbed too.\n'
    'What a mess!\n'
)
wrapper = TextWrapper(45, fix_sentence_endings=True)
show('paragraph wrap', wrapper.wrap(paragraph))
show('paragraph fill', wrapper.fill(paragraph))
show('sentence short', wrap('A short line. Note the single space.', 60,
                            fix_sentence_endings=True))
show('sentence quotes', wrap('I say, chaps! Anyone for "tennis?"\nHmmph!', 60,
                             fix_sentence_endings=True))
show('sentence abbreviation', wrap('File stdio.h is nice.', 60,
                                   fix_sentence_endings=True))

hyphenated = "this-is-a-useful-feature-for-reformatting-posts-from-tim-peters'ly"
for width in (40, 41, 42):
    show('hyphenated ' + str(width), wrap(hyphenated, width))
show('hyphenated no long break', wrap(hyphenated, 1, break_long_words=False))
for enabled in (True, False):
    show('hyphen mode ' + str(enabled), wrap('yaba daba-doo', 10,
                                            break_on_hyphens=enabled))
show('hyphenated numbers', wrap(
    'Python 1.0.0 was released on 1994-01-26.  Python 1.0.1 was\n'
    'released on 1994-02-15.', 30))
show('dash', wrap('Em-dashes should be written -- thus.', 25))
show('drop false', wrap(' This is a    sentence with     much whitespace.', 10,
                        drop_whitespace=False))
show('whitespace only', wrap('   ', 6))
show('whitespace only retained', wrap('   ', 6, drop_whitespace=False))
show('whitespace only indented', wrap('   ', 6, drop_whitespace=False,
                                     initial_indent='  '))
show('whitespace line retained', wrap('abcd    efgh', 6, drop_whitespace=False))
show('whitespace line dropped', wrap('abcd    efgh', 6))

# LongWordTestCase and LongWordWithHyphensTestCase.
long_text = ('Did you say "supercalifragilisticexpialidocious?"\n'
             'How *do* you spell that odd word, anyways?\n')
show('long word', wrap(long_text, 30))
show('long word unbroken', wrap(long_text, 30, break_long_words=False))
show('long word max lines', wrap(long_text, 12, max_lines=4))
show('indent exceeds width', wrap('-' * 10 + 'hello', 10,
                                  subsequent_indent=' ' * 15))
chemical = ('We used enyzme 2-succinyl-6-hydroxy-2,4-cyclohexadiene-1-carboxylate '
            'synthase.\n')
for break_long in (True, False):
    for break_hyphens in (True, False):
        show('long hyphens ' + str((break_long, break_hyphens)),
             wrap(chemical, 10, break_long_words=break_long,
                  break_on_hyphens=break_hyphens))

# MaxLinesTestCase, including placeholder backtracking and oversized placeholders.
for max_lines in (0, 1, 2, 4, 6):
    show('max lines ' + str(max_lines), wrap(text, 12, max_lines=max_lines))
show('custom placeholder', wrap(text, 12, max_lines=2, placeholder='...'))
show('placeholder backtrack', wrap('Good grief Python features are advancing quickly!',
                                   12, max_lines=3, placeholder='*****'))
show('placeholder indentation', wrap(text, 16, max_lines=2,
                                     initial_indent='    ', subsequent_indent='  ',
                                     placeholder=' [truncated]...'))
show('unused placeholder', wrap(text, 80, placeholder='.' * 1000))

# IndentTestCases: fill, hanging indents and mutable TextWrapper configuration.
indented_paragraph = (
    'This paragraph will be filled, first without any indentation,\n'
    'and then with some (including a hanging indent).'
)
show('fill', fill(indented_paragraph, 40))
show('fill initial indent', fill(indented_paragraph, 40, initial_indent='     '))
show('fill hanging indent', fill(indented_paragraph, 40,
                                 initial_indent='  * ', subsequent_indent='    '))
wrapper = TextWrapper(10)
show('wrapper before', wrapper.wrap('yaba daba-doo'))
wrapper.break_on_hyphens = False
show('wrapper changed hyphens', wrapper.wrap('yaba daba-doo'))
wrapper.width = 20
wrapper.initial_indent = '> '
wrapper.subsequent_indent = '  '
show('wrapper changed width', wrapper.fill('yaba daba-doo'))

# DedentTestCase: blank lines, declining margins and non-equivalent spaces/tabs.
for source in (
    '',
    'Hello there.\n\n  Boo!\n',
    '  Hello there.\n  \n  How are ya?\n  Oh good.\n',
    '  Foo\n    Bar\n \n   Baz\n',
    '     Foo\n    \n    Bar\n',
    '  hello\tthere\n  how are\tyou?',
    '        hello there\n\thow are you?',
    '  \thello there\n  \t  how are you?',
    "  \thello there\n   \thow are you?\n \tI'm fine, thanks",
):
    show('dedent', dedent(source))

# IndentTestCase: line-ending preservation, predicates and round trips.
for source in (
    'Hi.\nThis is a test.\nTesting.',
    'Hi.\nThis is a test.\n\nTesting.',
    '\nHi.\nThis is a test.\nTesting.\n',
    'Hi.\r\nThis is a test.\r\nTesting.\r\n',
    '\nHi.\r\nThis is a test.\n\r\nTesting.\r\n\n',
):
    show('indent default', indent(source, '  '))
    show('indent explicit default', indent(source, '  ', None))
    show('indent all', indent(source, '  ', lambda line: True))
    show('indent none', indent(source, '  ', lambda line: False))
    show('indent empty', indent(source, '  ', lambda line: not line.strip()))
for prefix in ('    ', '\t\t', ' \t  \t '):
    source = '\nHi.\nThis is a test.\nTesting.\n'
    assert dedent(indent(source, prefix)) == source
show('indent round trips', True)

# ShortenTestCase: whitespace collapse, exact widths and placeholder behavior.
short_text = "Hello there, how are you this fine day? I'm glad to hear it!"
for width in (18, len(short_text), len(short_text) - 1):
    show('shorten ' + str(width), shorten(short_text, width))
show('shorten custom', shorten(short_text, 18, placeholder='$$'))
show('shorten spaced placeholder', shorten(short_text, 18, placeholder=' $$'))
show('shorten empty', shorten('', 6))
for width in (12, 11, 10):
    show('shorten spaces ' + str(width), shorten('hello      world!  ', width))
show('shorten long first word', shorten('Helloo', 5))
show('shorten exact placeholder', shorten('x' * 20, 8, placeholder='(......)'))

# Upstream Unicode regressions plus a code-point (not display-width) sample.
show('umlaut', wrap('Die Empf\xe4nger-Auswahl', 13))
show('umlaut hyphen', wrap('aa \xe4\xe4-\xe4\xe4', 7))
for space in ('\u00a0', '\u202f'):
    for enabled in (True, False):
        show('nonbreaking ' + ascii(space) + ' ' + str(enabled),
             wrap('This is a sentence with non-breaking' + space + 'space.', 20,
                  break_on_hyphens=enabled))
show('unicode code points', wrap('\u4e2d\u6587 \u03b1\u03b2\u03b3 e\u0301', 4))
show('unicode shorten spaces', shorten('alpha\u00a0beta\u202fgamma', 12))

for width in (0, -1):
    error('bad width ' + str(width), wrap, "Whatever, it doesn't matter.", width)
error('bad empty width', wrap, '', 0)
error('bad initial placeholder', wrap, text, 16, initial_indent='    ',
      max_lines=1, placeholder=' [truncated]...')
error('bad subsequent placeholder', wrap, text, 16, subsequent_indent='    ',
      max_lines=2, placeholder=' [truncated]...')
error('bad shorten placeholder', shorten, 'x' * 20, 8, placeholder='(.......)')

# One callback/GC sample exercises indent's ordinary Python predicate path.
seen = []


def select(line):
    gc.collect()
    seen.append(line)
    return line.startswith('keep')


show('indent callback', indent('keep\n\nskip\nkeep too\n', '> ', select))
show('indent callback lines', seen)
