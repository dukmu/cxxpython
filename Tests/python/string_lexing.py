print("double")
print('single')
print("quote: '")
print('quote: "')
print("escape:\nline")

# full-line comments do not affect indentation
if True:
    # comment-only lines inside blocks do not create fake statements
    print("""triple
line""")
    print('''other
triple''')

print("tail")  # trailing comments are ignored
