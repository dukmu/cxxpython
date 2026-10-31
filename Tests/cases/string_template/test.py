from string import Template


def attempt(function, *args, **kwargs):
    try:
        print(function(*args, **kwargs))
    except Exception as exc:
        print(type(exc).__name__, str(exc))


t = Template('$who ${what} $$ $missing')
print(t.safe_substitute({'who': 'Ada', 'what': 42}))
attempt(t.substitute, {'who': 'Ada', 'what': 42})
print(t.substitute({'who': 'Ada', 'what': 42, 'missing': '!'}, what='yes'))
print(t.is_valid(), t.get_identifiers())
bad = Template('one\n$ok $! ${broken} $')
print(bad.is_valid(), bad.get_identifiers())
print(bad.safe_substitute(ok='OK'))
attempt(bad.substitute, ok='OK')
print(Template('$a ${a} $b $$ $a').get_identifiers())
print(Template('$é ${é}').is_valid())


class PercentTemplate(Template):
    delimiter = '%'


print(PercentTemplate('%name %%').substitute(name='value'))
