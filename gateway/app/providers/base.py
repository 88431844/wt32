from __future__ import annotations

from datetime import datetime
from typing import Protocol

from ..models import FuelData, StockQuote


class ProviderUnavailable(RuntimeError):
    def __init__(self, code: str, message: str, retryable: bool = True) -> None:
        super().__init__(message)
        self.code = code
        self.retryable = retryable


class StockProvider(Protocol):
    def fetch(self, symbol: str, now: datetime) -> StockQuote: ...


class FuelProvider(Protocol):
    def fetch(self, requested_region: str, province: str, now: datetime) -> FuelData: ...
