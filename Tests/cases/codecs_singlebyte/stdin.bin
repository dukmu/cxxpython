# All copied charmap modules use the unchanged CPython mapping tables.
names = ["charmap", "cp037", "cp1006", "cp1026", "cp1125", "cp1140", "cp1250", "cp1251", "cp1252", "cp1253", "cp1254", "cp1255", "cp1256", "cp1257", "cp1258", "cp273", "cp424", "cp437", "cp500", "cp720", "cp737", "cp775", "cp850", "cp852", "cp855", "cp856", "cp857", "cp858", "cp860", "cp861", "cp862", "cp863", "cp864", "cp865", "cp866", "cp869", "cp874", "cp875", "hp_roman8", "iso8859_1", "iso8859_10", "iso8859_11", "iso8859_13", "iso8859_14", "iso8859_15", "iso8859_16", "iso8859_2", "iso8859_3", "iso8859_4", "iso8859_5", "iso8859_6", "iso8859_7", "iso8859_8", "iso8859_9", "koi8_r", "koi8_t", "koi8_u", "kz1048", "mac_arabic", "mac_croatian", "mac_cyrillic", "mac_farsi", "mac_greek", "mac_iceland", "mac_latin2", "mac_roman", "mac_romanian", "mac_turkish", "palmos", "ptcp154", "tis_620"]
raw = bytes(range(256))
for name in names:
    decoded = raw.decode(name, 'replace')
    encoded = decoded.encode(name, 'replace')
    print(name, len(decoded), sum(ord(c) for c in decoded), len(encoded), sum(encoded))
