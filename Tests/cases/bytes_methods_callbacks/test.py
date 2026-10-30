class Start:
    def __init__(self, target, grow):
        self.target = target
        self.grow = grow

    def __index__(self):
        if self.grow:
            self.target.extend(b'd')
        else:
            self.target[0] = 100
        return 0


data = bytearray(b'abc')
print(data.find(b'd', Start(data, True)), data)
data = bytearray(b'abc')
print(data.find(b'd', Start(data, False)), data)


class ByteNeedle(bytearray):
    def __index__(self):
        return 97


print(b'abc'.find(ByteNeedle(b'bc')))
