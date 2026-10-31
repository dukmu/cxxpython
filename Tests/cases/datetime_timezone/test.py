import gc
from datetime import datetime, timedelta, timezone, tzinfo


class Zone(tzinfo):
    def utcoffset(self, value):
        gc.collect()
        return timedelta(hours=2, minutes=30)

    def dst(self, value):
        gc.collect()
        return timedelta(minutes=30)

    def tzname(self, value):
        gc.collect()
        return 'CUSTOM'


zone = Zone()
value = datetime(2024, 4, 5, 12, 34, 56, tzinfo=zone)
print('callbacks', value.utcoffset(), value.dst(), value.tzname())
print('convert', value.astimezone(timezone.utc), value.timestamp())
print('fixed', timezone(timedelta(hours=-5), 'west'), timezone(timedelta(hours=3)).tzname(None))
print('replace', value.replace(tzinfo=timezone.utc).isoformat(), value.fold, value.replace(fold=1).fold)
print('compare', value == datetime(2024, 4, 5, 10, 4, 56, tzinfo=timezone.utc))
print('hash', hash(value) == hash(datetime(2024, 4, 5, 10, 4, 56, tzinfo=timezone.utc)))
