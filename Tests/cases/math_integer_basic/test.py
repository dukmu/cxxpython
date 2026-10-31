import math

print(math.gcd(), math.lcm(), math.gcd(-42), math.lcm(-42))
print(math.gcd(0, -84, 126), math.lcm(0, 17), math.lcm(-12, 18, 35))
x = (1 << 1024) - 1
y = (1 << 768) - 1
print(math.gcd(x, y) == (1 << 256) - 1)
print(math.lcm(x, y) * math.gcd(x, y) == x * y)
print(math.factorial(200) == math.perm(200) == math.perm(200, None))
print(math.comb(1000, 500) * math.factorial(500) ** 2 == math.factorial(1000))
print(math.perm(1000, 300) == math.factorial(1000) // math.factorial(700))
huge = 1 << 100
print(math.comb(huge, huge - 2) == huge * (huge - 1) // 2)
print(math.perm(huge, 2) == huge * (huge - 1))
print(math.comb(huge, huge + 1), math.perm(huge, huge + 1))
for f, args in [(math.factorial, (-1,)), (math.factorial, (huge,)),
                (math.comb, (-1, 2)), (math.comb, (3, -1)),
                (math.perm, (huge, huge)), (math.comb, (huge, huge // 2)),
                (math.gcd, (1, 2.0)), (math.factorial, (3.0,))]:
    try:
        f(*args)
    except Exception as exc:
        print(type(exc).__name__)
    else:
        print('missing exception')
