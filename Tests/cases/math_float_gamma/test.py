import math

for x in [0.5, 1.0, 2.0, 5.0, 10.0, -0.5, -1.5, 20.5, 1e-25]:
    print(math.gamma(x), math.lgamma(x))
for x in [0.0, -0.0, -1.0, -2.0, -math.inf, math.inf, math.nan, 200.0]:
    for function in [math.gamma, math.lgamma]:
        try:
            result = function(x)
            print('nan' if math.isnan(result) else result)
        except (ValueError, OverflowError) as error:
            print(type(error).__name__)
for x in [-math.inf, -1.0, -0.0, 0.5, 1.0, 2.0, math.inf, math.nan]:
    for function in [math.asinh, math.acosh, math.atanh]:
        try:
            result = function(x)
            print('nan' if math.isnan(result) else result)
        except ValueError as error:
            print(type(error).__name__)
