import stat, keyword, token
for mode in (stat.S_IFREG | 0o644, stat.S_IFDIR | 0o755, stat.S_IFLNK | 0o777, stat.S_IFREG | 0o4755):
    print(stat.S_ISREG(mode), stat.S_ISDIR(mode), stat.S_IMODE(mode), stat.filemode(mode))
print(keyword.iskeyword('for'), keyword.iskeyword('match'), keyword.issoftkeyword('match'), keyword.issoftkeyword('type'))
print(token.tok_name[token.NAME], token.EXACT_TOKEN_TYPES[':='], token.ISTERMINAL(token.NAME), token.ISNONTERMINAL(256), token.ISEOF(token.ENDMARKER))
