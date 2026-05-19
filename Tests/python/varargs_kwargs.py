def collect(first, *args, **kwargs):
    print(first)
    print(args)
    print(kwargs)

collect('a', 'b', 'c', extra='value')
