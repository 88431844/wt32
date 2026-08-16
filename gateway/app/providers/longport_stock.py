from __future__ import annotations

from datetime import date, datetime, timezone
from typing import Any

from ..models import MarketPoint, StockQuote
from .base import ProviderUnavailable


def _value(obj: Any, name: str, default: Any = None) -> Any:
    if isinstance(obj, dict):
        return obj.get(name, default)
    return getattr(obj, name, default)


class LongPortStockProvider:
    def __init__(self, context: Any | None = None, context_factory: Any | None = None) -> None:
        self.context = context
        self.context_factory = context_factory

    def fetch(self, symbol: str, now: datetime) -> StockQuote:
        if self.context is None and self.context_factory is not None:
            self.context = self.context_factory()
        if self.context is None:
            raise ProviderUnavailable("stock_provider_unconfigured", "LongPort credentials are not configured.", False)
        try:
            raw = self.context.quote([symbol])[0]
            price = float(_value(raw, "last_done"))
            previous = float(_value(raw, "prev_close"))
            opening = float(_value(raw, "open"))
            high = float(_value(raw, "high"))
            low = float(_value(raw, "low"))
            candles = self.context.today_candlesticks(symbol, "minute", "no_adjust")
            points = []
            for candle in candles:
                timestamp = _value(candle, "timestamp")
                minute = int((datetime.fromtimestamp(float(timestamp), tz=timezone.utc).astimezone(now.tzinfo)).hour * 60 + datetime.fromtimestamp(float(timestamp), tz=timezone.utc).astimezone(now.tzinfo).minute) if timestamp is not None else len(points)
                points.append(MarketPoint(minute=minute, price=float(_value(candle, "close"))))
            points = points[-242:]
            return StockQuote(
                symbol=symbol,
                name="比亚迪" if symbol.startswith("002594") else symbol,
                market_status="closed",
                trading_date=now.date(),
                price=price,
                previous_close=previous,
                open=opening,
                close=price,
                high=high,
                low=low,
                change=round(price - previous, 4),
                change_percent=round((price - previous) / previous * 100, 4),
                updated_at=now,
                delayed=True,
                points=points,
            )
        except ProviderUnavailable:
            raise
        except Exception as exc:
            raise ProviderUnavailable("stock_provider_error", f"LongPort request failed: {exc}") from exc
