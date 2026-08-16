from datetime import datetime
from types import SimpleNamespace
from zoneinfo import ZoneInfo

from app.providers.longport_stock import LongPortStockProvider


class FakeQuoteContext:
    def quote(self, symbols):
        return [SimpleNamespace(symbol=symbols[0], last_done=112.8, prev_close=111.22, open=111.26, high=114.05, low=110.91)]

    def today_candlesticks(self, symbol, period, adjust):
        return [SimpleNamespace(close=111.26, timestamp=1723626000), SimpleNamespace(close=112.8, timestamp=1723636800)]


def test_longport_maps_quote_and_history():
    now = datetime(2026, 8, 14, 15, 5, tzinfo=ZoneInfo("Asia/Shanghai"))
    provider = LongPortStockProvider(context=FakeQuoteContext())
    quote = provider.fetch("002594.SZ", now)
    assert quote.symbol == "002594.SZ"
    assert quote.price == 112.8
    assert quote.close == 112.8
    assert quote.points

