import tokenize

tokens = tokenize.tokenize("x = 1\nif x:\n    pass\n")
print(tokens[0].type)
print(tokens[0].string)
print(tokens[0].start)
print(tokens[2].type)
print(tokens[3].type)
print(tokens[6].type)
print(tokens[-1].type)
print(tokenize.__all__.__contains__('generate_tokens'))

lines = ["a = 2\n", ""]

def readline():
    value = lines[0]
    del lines[0]
    return value

generated = tokenize.generate_tokens(readline)
print(generated[0].string)
print(generated[2].string)
