#!/usr/bin/env python3
"""Extract Unicode tables from CPython 3.12.14's generated data.

CPython is distributed under the PSF license; see doc/licenses/python-3.12.14.txt.
Usage: python extract_unicodetype.py CPYTHON_SOURCE OUTPUT_DIRECTORY
"""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
output = pathlib.Path(sys.argv[2])
source = root / "Objects" / "unicodetype_db.h"
data = source.read_text()
records = data[data.index("const _PyUnicode_TypeRecord"):data.index("const Py_UCS4 _PyUnicode_ExtendedCase")]
indexes = data[data.index("#define SHIFT"):data.index("/* Returns the numeric value")]
numeric = data[data.index("double _PyUnicode_ToNumeric"):data.index("/* Returns 1 for Unicode characters having the bidirectional")]
(output / "unicodedata_type_db.hpp").write_text(
    "// Extracted from CPython 3.12.14 Objects/unicodetype_db.h (PSF license).\n"
    + (records + indexes + numeric).rstrip() + "\n"
)
database = (root / "Modules" / "unicodedata_db.h").read_text()
database = database.replace("static struct reindex nfc_", "static const struct reindex nfc_")
database = database.replace("const char *_PyUnicode_", "static const char *const _PyUnicode_")
database = database.replace("static const char *decomp_prefix", "static const char *const decomp_prefix")
(output / "unicodedata_db.hpp").write_text(
    "// Extracted from CPython 3.12.14 Modules/unicodedata_db.h (PSF license).\n"
    + database.rstrip() + "\n"
)

# The parser's name API intentionally returns only individual characters.
# Extract names of the upstream private-use entries for named sequences.
names = (root / "Modules" / "unicodename_db.h").read_text()
def array(name):
    body = re.search(r"\b" + name + r"\[\] = \{(.*?)\};", names, re.S).group(1)
    return [int(value, 0) for value in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]

lexicon, offsets = array("lexicon"), array("lexicon_offset")
book, first, second = array("phrasebook"), array("phrasebook_offset1"), array("phrasebook_offset2")
sequence_text = re.search(r"named_sequences\[\] = \{(.*?)\};", names, re.S).group(1)
sequences = re.findall(r"\{(\d+), \{([^}]+)\}\}", sequence_text)
def sequence_name(code):
    at = second[(first[code >> 7] << 7) + (code & 127)]
    words = []
    while True:
        word = book[at]
        at += 1
        if word >= 190:
            word = ((word - 190) << 8) + book[at]
            at += 1
        position = offsets[word]
        chars = []
        while lexicon[position] < 128:
            chars.append(chr(lexicon[position]))
            position += 1
        last = lexicon[position]
        if last != 128:
            chars.append(chr(last & 127))
        words.append("".join(chars))
        if last == 128:
            return " ".join(words)

rows = []
for index, (length, values) in enumerate(sequences):
    points = [int(value.strip(), 0) for value in values.split(",")]
    assert len(points) == int(length)
    name = sequence_name(0xf0200 + index)
    rows.append((name, points))
rows.sort()
(output / "unicodedata_sequences.hpp").write_text(
    "// Extracted from CPython 3.12.14 Modules/unicodename_db.h (PSF license).\n"
    + "static constexpr NamedSequence named_sequences[] = {\n"
    + "".join('    {"%s", U"%s"},\n' % (name, "".join("\\U%08X" % point for point in points))
              for name, points in rows)
    + "};\n"
)
