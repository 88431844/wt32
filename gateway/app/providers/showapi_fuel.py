from __future__ import annotations

from datetime import datetime
from typing import Any

import httpx

from ..models import FuelData, FuelPrice
from .base import FuelProvider, ProviderUnavailable


class ShowApiFuelProvider:
    endpoint = "https://route.showapi.com/138-46"

    def __init__(self, app_key: str, client: Any | None = None) -> None:
        self.app_key = app_key
        self.client = client or httpx.Client()

    def fetch(self, requested_region: str, province: str, now: datetime) -> FuelData:
        if not self.app_key:
            raise ProviderUnavailable("fuel_provider_unconfigured", "ShowAPI app key is not configured.", False)
        try:
            response = self.client.post(self.endpoint, data={"appKey": self.app_key, "prov": province}, timeout=8.0)
            response.raise_for_status()
            payload = response.json()
            body = payload.get("showapi_res_body", {})
            rows = body.get("list", [])
            if payload.get("showapi_res_code") not in (None, 0) or not rows:
                raise ValueError("ShowAPI returned no fuel prices")
            row = rows[0]
            updated_at = datetime.strptime(row["ct"], "%Y-%m-%d %H:%M:%S").replace(tzinfo=now.tzinfo)
            prices = [FuelPrice(grade="92#", price=float(row["p92"])), FuelPrice(grade="95#", price=float(row["p95"]))]
            return FuelData(requested_region=requested_region, province=province, updated_at=updated_at, prices=prices)
        except ProviderUnavailable:
            raise
        except Exception as exc:
            raise ProviderUnavailable("fuel_provider_error", f"ShowAPI request failed: {exc}") from exc
