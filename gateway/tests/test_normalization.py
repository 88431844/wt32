import pytest

from app.providers.normalization import normalize_fuel_region, normalize_stock_symbol


@pytest.mark.parametrize(
    ("raw", "expected"),
    [("002594", "002594.SZ"), ("002594.sz", "002594.SZ"), ("600519", "600519.SH")],
)
def test_normalize_a_share_symbol(raw: str, expected: str) -> None:
    assert normalize_stock_symbol(raw) == expected


def test_shenzhen_normalizes_to_guangdong() -> None:
    assert normalize_fuel_region("深圳") == ("深圳", "广东")


@pytest.mark.parametrize("raw", ["", "  ", "00259", "002594;rm -rf", "ABC123"])
def test_reject_unsafe_stock_symbol(raw: str) -> None:
    with pytest.raises(ValueError):
        normalize_stock_symbol(raw)


def test_unknown_fuel_location_is_rejected() -> None:
    with pytest.raises(ValueError):
        normalize_fuel_region("火星基地")
