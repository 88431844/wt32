from __future__ import annotations

import re


_STOCK_CODE = re.compile(r"^[0-9]{6}$")
_SZ_PREFIXES = ("000", "001", "002", "003", "300", "301")
_SH_PREFIXES = ("600", "601", "603", "605", "688", "689")

_REGIONS = {
    "深圳": "广东",
    "广州": "广东",
    "珠海": "广东",
    "佛山": "广东",
    "东莞": "广东",
    "北京": "北京",
    "上海": "上海",
    "杭州": "浙江",
    "南京": "江苏",
    "苏州": "江苏",
    "成都": "四川",
    "重庆": "重庆",
    "武汉": "湖北",
    "西安": "陕西",
    "长沙": "湖南",
    "厦门": "福建",
    "福州": "福建",
    "济南": "山东",
    "郑州": "河南",
    "天津": "天津",
}


def normalize_stock_symbol(raw: str) -> str:
    value = raw.strip().upper()
    if "." in value:
        code, suffix = value.split(".", 1)
        if not _STOCK_CODE.fullmatch(code) or suffix not in {"SZ", "SH"}:
            raise ValueError("stock symbol must be a six-digit A-share code")
        return f"{code}.{suffix}"
    if not _STOCK_CODE.fullmatch(value):
        raise ValueError("stock symbol must be a six-digit A-share code")
    if value.startswith(_SZ_PREFIXES):
        return f"{value}.SZ"
    if value.startswith(_SH_PREFIXES):
        return f"{value}.SH"
    raise ValueError("unsupported A-share market prefix")


def normalize_fuel_region(raw: str) -> tuple[str, str]:
    requested = raw.strip()
    if not requested:
        raise ValueError("fuel region cannot be blank")
    normalized = requested.removesuffix("市").removesuffix("省")
    province = _REGIONS.get(normalized)
    if province is None:
        province = normalized if normalized in set(_REGIONS.values()) else None
    if province is None:
        raise ValueError(f"unknown fuel region: {raw}")
    return normalized, province
