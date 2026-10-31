import locale

print(locale.setlocale(locale.LC_ALL, 'C'))
print(locale.localeconv()['decimal_point'], repr(locale.localeconv()['thousands_sep']))
print(locale.format_string('%.2f', 1234.5, grouping=True))
print(locale.atof('12.5'), locale.atoi('42'))
print(locale.normalize('C'))
