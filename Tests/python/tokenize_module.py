import tokenize
import token

source = ["x = 1\n", "if x:\n", "    pass\n", ""]

def readline():
    value = source[0]
    del source[0]
    return value

tokens = tokenize.generate_tokens(readline)
print(tokens[0].type)
print(tokens[0].string)
print(tokens[0].start)
print(tokens[2].type)
print(tokens[3].type)
print(tokens[6].type)
print(tokens[6].exact_type)
print(tokens[-1].type)
print(tokenize.__all__.__contains__('generate_tokens'))
print(tokenize.__all__.__contains__('NAME'))
print(token.NAME)
print(token.OP)
print(token.EXACT_TOKEN_TYPES['='])
print(tokens[1].type)
print(tokens[1].exact_type)

lines = ["a = 2\n", ""]

def readline():
    value = lines[0]
    del lines[0]
    return value

generated = tokenize.generate_tokens(readline)
print(generated[0].string)
print(generated[2].string)
