import gc
import ipaddress
from urllib.parse import (parse_qs, parse_qsl, quote, quote_from_bytes,
                          unquote_to_bytes, urlencode, urljoin, urlsplit)


print('quote', quote('caf\u00e9 /?#', safe='/'))
print('bytes', quote_from_bytes(b'/a b/\xff', safe=b'/'))
print('unquote', unquote_to_bytes('/a%20b/%FF'))
url = urlsplit('https://user:pass@[2001:db8::1]:8443/a?x=1#part')
print('split', url.scheme, url.hostname, url.port, url.path, url.query, url.fragment)
print('bytes split', urlsplit(b'https://example.com/a?x=1').geturl())
print('join', urljoin('https://example.com/a/b', '../c?q=2'))
print('query', parse_qs('x=1&x=2&blank=', keep_blank_values=True))
print('pairs', parse_qsl('x=1&blank=', keep_blank_values=True))

calls = []


def quote_callback(value, safe, encoding=None, errors=None):
    gc.collect()
    calls.append(value)
    return quote(value, safe=safe, encoding=encoding, errors=errors)


print('encoded', urlencode([('name', 'a b'), ('items', ['x/y', 'z'])],
                           doseq=True, quote_via=quote_callback))
print('callbacks', calls)


class CallbackError(Exception):
    pass


callback_error = CallbackError('quote callback')


def failing_quote(*args):
    gc.collect()
    raise callback_error


try:
    urlencode([('key', 'value')], quote_via=failing_quote)
except CallbackError as error:
    print('callback exception', error is callback_error)
else:
    raise AssertionError('quote callback error was lost')

v4 = ipaddress.ip_address('192.0.2.1')
v6 = ipaddress.ip_address('2001:db8::1')
print('IPv4', v4.version, str(v4), int(v4), v4.packed)
print('IPv6', v6.version, v6.compressed, v6.exploded, v6.packed.hex())
print('reverse', v4.reverse_pointer)
network = ipaddress.ip_network('192.0.2.0/30')
print('network', str(network), str(network.netmask), str(network.broadcast_address))
print('hosts', [str(address) for address in network.hosts()])
print('membership', v4 in network, v6 in network)
print('subnets', [str(subnet) for subnet in network.subnets(prefixlen_diff=1)])
interface = ipaddress.ip_interface('2001:db8::1/64')
print('interface', str(interface.ip), str(interface.network))

for name, operation in (
        ('brackets', lambda: urlsplit('https://[broken/')),
        ('port', lambda: urlsplit('https://example.com:65536/').port),
        ('query', lambda: parse_qsl('broken', strict_parsing=True)),
        ('address', lambda: ipaddress.ip_address('192.168.001.1')),
        ('host bits', lambda: ipaddress.ip_network('192.0.2.1/24'))):
    try:
        operation()
    except ValueError:
        print('rejected', name)
    else:
        raise AssertionError('accepted invalid ' + name)
