from datetime import datetime
from zoneinfo import ZoneInfo

from app.providers.showapi_fuel import ShowApiFuelProvider


class FakeResponse:
    def raise_for_status(self):
        return None

    def json(self):
        return {"showapi_res_code": 0, "showapi_res_body": {"list": [{"p92": "7.80", "p95": "8.45", "ct": "2026-08-15 07:00:00"}]}}


class FakeClient:
    def __init__(self):
        self.calls = []

    def post(self, url, data, timeout):
        self.calls.append((url, data, timeout))
        return FakeResponse()


def test_showapi_maps_guangdong_and_returns_two_grades():
    client = FakeClient()
    now = datetime(2026, 8, 16, tzinfo=ZoneInfo("Asia/Shanghai"))
    result = ShowApiFuelProvider("app-key", client=client).fetch("深圳", "广东", now)
    assert [p.grade for p in result.prices] == ["92#", "95#"]
    assert result.province == "广东"
    assert client.calls[0][1]["prov"] == "广东"
