import gettext
import io
import struct

null = gettext.NullTranslations()
print(null.gettext('hello'), null.ngettext('item', 'items', 2))
print(null.pgettext('ctx', 'hello'), null.npgettext('ctx', 'item', 'items', 1))
messages = [
    (b'', b'Content-Type: text/plain; charset=UTF-8\nPlural-Forms: nplurals=2; plural=(n != 1);\n'),
    (b'apple\0apples', b'pomme\0pommes'),
    (b'ctx\x04hello', b'bonjour'),
]
count = len(messages)
offset = 28 + count * 16
originals = b''
translations = b''
otable = []
ttable = []
for original, translated in messages:
    otable.append((len(original), offset + len(originals)))
    originals += original + b'\0'
offset += len(originals)
for original, translated in messages:
    ttable.append((len(translated), offset + len(translations)))
    translations += translated + b'\0'
data = struct.pack('<7I', 0x950412de, 0, count, 28, 28 + count * 8, 0, 0)
for entry in otable + ttable:
    data += struct.pack('<2I', *entry)
catalog = gettext.GNUTranslations(io.BytesIO(data + originals + translations))
print(catalog.ngettext('apple', 'apples', 1), catalog.ngettext('apple', 'apples', 3))
print(catalog.pgettext('ctx', 'hello'), catalog.gettext('missing'))
try:
    gettext.GNUTranslations(io.BytesIO(b'bad catalog'))
except Exception as exc:
    print(type(exc).__name__)
