import hashlib
import _blake2

print(hashlib.blake2b is _blake2.blake2b, type(hashlib.blake2b()) is hashlib.blake2b)
print(hashlib.blake2s.MAX_DIGEST_SIZE, hashlib.blake2s.MAX_KEY_SIZE, hashlib.blake2s.SALT_SIZE, hashlib.blake2s.PERSON_SIZE)

print(hashlib.md5(b"abc").hexdigest())
print(hashlib.sha1(b"abc").hexdigest())
print(hashlib.sha224(b"abc").hexdigest())
h = hashlib.sha256(b"a")
h.update(bytearray(b"bc"))
print(h.hexdigest())
view_hash = hashlib.sha256()
view_hash.update(memoryview(bytearray(b"abc")))
print(view_hash.hexdigest())
copy = h.copy()
copy.update(b"d")
print(h.hexdigest())
print(copy.hexdigest())
print(hashlib.sha384(b"abc").hexdigest())
print(hashlib.sha512(b"abc").hexdigest())
print(hashlib.sha3_256(b"abc").hexdigest())
print(hashlib.shake_128(b"abc").hexdigest(16))
print(hashlib.pbkdf2_hmac("sha256", b"password", b"salt", 1).hex())
print(hashlib.blake2b(b"abc", key=b"key", digest_size=16).hexdigest())
print(hashlib.blake2s(b"abc", salt=b"salt", person=b"person").hexdigest())
print(hashlib.blake2b(b"abc", fanout=2, depth=2, leaf_size=64, node_offset=3, node_depth=1, inner_size=16, last_node=True).hexdigest())
try:
    hashlib.new("no_such_hash")
except ValueError:
    print("unknown hash")
