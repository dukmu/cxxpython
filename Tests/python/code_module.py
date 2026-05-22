import code


class Writer:
    def __init__(self):
        self.text = ''

    def write(self, text):
        self.text = self.text + text

    def flush(self):
        pass


out = Writer()
err = Writer()
scope = {'__name__': '__console__'}
console = code.InteractiveConsole(scope, '<console-test>', out, err)

print(console.push('1 + 2'))
print(out.text)
out.text = ''

print(console.push('x = "ok"'))
print(console.push('x'))
print(out.text)
out.text = ''

print(console.push('def f():'))
print(console.push('    return 7'))
print(console.push(''))
print(console.push('f()'))
print(out.text)
out.text = ''

print(console.push('try:'))
print(console.push("    raise TypeError('boom')"))
print(console.push('except TypeError as exc:'))
print(console.push('    print(exc)'))
print(console.push(''))
print(out.text)
out.text = ''

print(console.push('missing'))
print(err.text.__contains__('Traceback (most recent call last):'))
print(err.text.__contains__('File "<console-test>", line 1, in '))
print(err.text.__contains__("NameError: name 'missing' is not defined"))
