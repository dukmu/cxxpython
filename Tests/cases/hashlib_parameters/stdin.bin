import hashlib

print(hashlib.scrypt(b"", salt=b"", n=16, r=1, p=1, dklen=64).hex())
for algorithm in (hashlib.sha256, hashlib.shake_128, hashlib.blake2b, hashlib.blake2s):
    digest = algorithm(b"abc", usedforsecurity=False)
    if algorithm is hashlib.shake_128:
        print(digest.digest(17) == digest.copy().digest(17), digest.hexdigest(17) == digest.hexdigest(17))
    else:
        print(digest.digest() == digest.copy().digest(), digest.hexdigest() == digest.hexdigest())

class Flag:
    def __bool__(self):
        raise RuntimeError("flag error")

for algorithm in (hashlib.sha256, hashlib.blake2b, hashlib.blake2s):
    try:
        algorithm(usedforsecurity=Flag())
    except RuntimeError as error:
        print(type(error).__name__, str(error))
try:
    hashlib.new("sha256", usedforsecurity=Flag())
except RuntimeError as error:
    print(type(error).__name__, str(error))

for function in (
    lambda: hashlib.pbkdf2_hmac("sha256", b"p", b"s", 0),
    lambda: hashlib.pbkdf2_hmac("sha256", b"p", b"s", 1, dklen=0),
    lambda: hashlib.scrypt(b"p", salt=b"s", n=3, r=1, p=1),
):
    try:
        function()
    except ValueError:
        print("invalid parameters")
