import marshal

shared = [1234, 'shared value']
value = [shared, shared]
result = marshal.loads(marshal.dumps(value))
print(result == value, result[0] is result[1])

value = []
value.append(value)
result = marshal.loads(marshal.dumps(value))
print(result is result[0])

value = {}
value['self'] = value
result = marshal.loads(marshal.dumps(value))
print(result is result['self'])

items = []
value = (items,)
items.append(value)
result = marshal.loads(marshal.dumps(value))
print(result is result[0][0])

# Reference index zero points to the containing list.
result = marshal.loads(b'\xdb\x01\x00\x00\x00r\x00\x00\x00\x00')
print(result is result[0])
