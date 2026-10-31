import gc
import io
import json
import threading


value = {
    "nested": [True, False, None, -12, 3.5, {"text": "line\n\"\\\t"}],
    "unicode": "caf\u00e9 \U0001f600",
}
encoded = json.dumps(value, sort_keys=True)
print(encoded)
print(json.loads(encoded) == value)
print(json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")))
print(json.dumps({"b": [1, 2], "a": None}, indent=2, sort_keys=True))

surrogates = json.loads('"\\ud83d\\ude00\\ud800"')
print(json.dumps(surrogates))
print(json.loads(json.dumps(surrogates)) == surrogates)


def parse_int(text):
    gc.collect()
    return "int:" + text


def parse_float(text):
    gc.collect()
    return "float:" + text


def pairs_hook(pairs):
    gc.collect()
    return ["pairs", pairs]


print(json.loads('{"a": 12, "b": -2.5, "a": 3}',
                 parse_int=parse_int, parse_float=parse_float,
                 object_pairs_hook=pairs_hook))


class Custom:
    pass


def default(value):
    gc.collect()
    return {"custom": True}


print(json.dumps([Custom()], default=default, sort_keys=True))

for text in ('{"a":}', '[1,]', 'true false', '"unterminated'):
    try:
        json.loads(text)
    except json.JSONDecodeError as error:
        print(type(error).__name__, error.msg, error.pos, error.lineno, error.colno)

cycle = []
cycle.append(cycle)
try:
    json.dumps(cycle)
except ValueError as error:
    print(type(error).__name__, str(error))

try:
    json.dumps(Custom())
except TypeError as error:
    print(type(error).__name__, str(error))

stream = io.StringIO()
json.dump(value, stream, sort_keys=True)
stream.seek(0)
assert json.load(stream) == value
decoded, end = json.JSONDecoder().raw_decode('[1, true] rest')
assert decoded == [1, True] and end == 9
print('stream and raw decode')

errors = [None] * 4


def worker(index):
    try:
        for number in range(8):
            item = {'thread': index, 'number': number, 'items': [None, True, 'text']}
            text = json.dumps(item, sort_keys=True)
            gc.collect()
            assert json.loads(text) == item
    except BaseException as error:
        errors[index] = str(error)


threads = [threading.Thread(target=worker, args=(index,)) for index in range(4)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
assert errors == [None] * 4, errors
print('shared defaults across threads and collection')
