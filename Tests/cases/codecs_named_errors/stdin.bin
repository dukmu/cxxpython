import codecs
for text in ('\u00e9\U0001f600', '\uac00\u4e00', '\ud800\uffff\x00'):
    error = UnicodeEncodeError('ascii', text, 0, len(text), 'test')
    print(codecs.namereplace_errors(error))
error = ValueError('sentinel')
try:
    codecs.strict_errors(error)
except ValueError as caught:
    print(caught is error)
for value in (None, 3):
    try:
        codecs.strict_errors(value)
    except TypeError:
        print('exception required')
events = []
def handle(error):
    events.append((error.start, error.end))
    return '?', error.end
codecs.register_error('mapped_group', handle)
print('\u0100\u0101x\u0102'.encode('cp1252', 'mapped_group'), events)
