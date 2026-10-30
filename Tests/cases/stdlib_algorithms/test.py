import bisect
import heapq
import operator

a = [1, 2, 2, 5]
print(bisect.bisect_left(a, 2), bisect.bisect_right(a, 2))
bisect.insort(a, 3)
print(a)
records = [('a', 1), ('b', 3)]
bisect.insort_left(records, ('c', 2), key=operator.itemgetter(1))
print(records)
print(bisect.bisect_right(records, 2, key=operator.itemgetter(1)))

h = [9, 2, 7, 1, 5]
heapq.heapify(h)
heapq.heappush(h, 3)
print([heapq.heappop(h) for _ in range(len(h))])
h = [1, 4, 8]
heapq.heapify(h)
print(heapq.heappushpop(h, 2), heapq.heapreplace(h, 6), h)
print(list(heapq.merge([1, 4], [2, 3, 7], [])))
print(list(heapq.merge([9, 5], [8, 6], reverse=True)))
print(list(heapq.merge(['b', 'aa'], ['c', 'ddd'], key=len)))
print(heapq.nsmallest(3, [5, 2, 7, 1, 3]))
print(heapq.nlargest(2, records, key=operator.itemgetter(1)))

class C:
    def __init__(self):
        self.value = [4, 5]
    def method(self, x, *, y=1):
        return x + y
c = C()
print(operator.attrgetter('value')(c), operator.methodcaller('method', 3, y=4)(c))
print(operator.itemgetter(0, 2)([3, 4, 5]))
print(operator.call(lambda a, *, b: a * b, 6, b=7))
print(operator.index(True), operator.countOf([1, 2, 1], 1), operator.indexOf([1, 2], 2))
print(operator.add(3, 4), operator.mul(3, 4), operator.truediv(9, 2), operator.xor(3, 6))
a = [1]
print(operator.iadd(a, [2]) is a, a)
try:
    bisect.bisect_left([], 1, -1)
except ValueError:
    print('negative lo rejected')
