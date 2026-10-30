import operator


class Item:
    pass


for target in (Item(), Item):
    try:
        target.missing
    except AttributeError as error:
        print(str(error))
for target in ('3', None, Item()):
    try:
        operator.index(target)
    except TypeError as error:
        print(str(error))
for target in (1, None, Item()):
    try:
        iter(target)
    except TypeError as error:
        print(str(error))
    try:
        first, second = target
    except TypeError as error:
        print(str(error))


class UserError:
    def __iter__(self):
        raise TypeError('user iterator failure')


class DeletesIterator:
    def __iter__(self):
        del type(self).__iter__
        raise TypeError('removed iterator')


for target in (UserError(), DeletesIterator()):
    try:
        first, second = target
    except TypeError as error:
        print(str(error))
