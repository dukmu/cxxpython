import math

print(math.pi, math.e, math.tau)
print(math.isinf(math.inf), math.isnan(math.nan), math.isfinite(3))
print(math.modf(-1.25), math.modf(-0.0), math.modf(math.inf))
print(math.frexp(12.0), math.ldexp(0.75, 4))
print(math.fmod(-7, 4), math.remainder(7, 4), math.copysign(0, -1))
print(math.sqrt(9), math.sin(0), math.cos(0), math.exp(0), math.log(1))
print(math.floor(-1.25), math.ceil(-1.25), math.trunc(-1.25))
for function, value in [(math.sqrt, -1), (math.log, 0), (math.exp, 1000)]:
    try:
        function(value)
    except (ValueError, OverflowError) as error:
        print(type(error).__name__, str(error))
