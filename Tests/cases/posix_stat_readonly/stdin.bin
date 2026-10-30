import posix

result = posix.stat_result((0, 1, 2, 3, 4, 5, 6, 7, 8, 9))
for name in ("st_atime", "st_atime_ns", "st_blksize", "st_mode"):
    try:
        setattr(result, name, 42)
    except AttributeError:
        print(name, "readonly")
